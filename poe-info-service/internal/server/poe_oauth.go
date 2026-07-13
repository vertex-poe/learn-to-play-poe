package server

import (
	"context"
	"database/sql"
	"encoding/json"
	"log"
	"sync"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/creds"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
)

// poeOAuthCredKey is the internal/creds key the persisted PoE OAuth token
// set is stored under (ADR-004/ADR-005) — a structured JSON record. Unlike
// steamAPIKeyCredKey (a client-supplied secret via credentials.store), this
// service both originates and owns this credential: it runs the login flow
// itself via internal/poe.Login, per ADR-004's "poe-info-service may
// initiate the flow itself ... no client capability required".
const poeOAuthCredKey = "poeOAuthToken"

// poeOAuthRefreshRetryBackoff is how long to wait before retrying a failed
// background refresh (transient network error, momentary 5xx) rather than
// immediately forcing the user through a full interactive re-login — bounded
// by the same PastRefreshWindow check the next attempt makes regardless.
const poeOAuthRefreshRetryBackoff = 1 * time.Minute

// poeOAuthState is the server's live view of PoE OAuth: the last known
// token (nil if never authenticated, or after logout/an unrecoverable
// refresh failure), whether a login attempt is currently in flight, the
// most recent failure message (if any), and the pending refresh timer so a
// new login/logout/refresh can supersede a stale one.
type poeOAuthState struct {
	mu           sync.Mutex
	token        *poe.Token
	inProgress   bool
	lastError    string
	refreshTimer *time.Timer
}

// poeOAuthSnapshot is a point-in-time, lock-free copy of poeOAuthState,
// letting statusPayload be a pure function independent of locking — makes
// it directly unit-testable.
type poeOAuthSnapshot struct {
	token      *poe.Token
	inProgress bool
	lastError  string
}

// statusPayload builds the poe.oauth.status/TopicPoeOAuthStatus shape.
// Authorized reflects whether a stored token still has a usable (not
// PastRefreshWindow) refresh token as of now — not whether the access token
// itself is currently valid, since a background refresh transparently
// renews that; a client only needs to know whether re-login is required.
func (snap poeOAuthSnapshot) statusPayload(now time.Time) proto.PoeOAuthStatusPayload {
	payload := proto.PoeOAuthStatusPayload{
		InProgress: snap.inProgress,
		Error:      snap.lastError,
	}
	if snap.token != nil && !snap.token.PastRefreshWindow(now) {
		payload.Authorized = true
		payload.Username = snap.token.Username
		payload.Scope = snap.token.Scope
		payload.AccessExpiration = snap.token.AccessExpiration
	}
	return payload
}

func (s *server) poeOAuthSnapshot() poeOAuthSnapshot {
	s.poeOAuth.mu.Lock()
	defer s.poeOAuth.mu.Unlock()
	return poeOAuthSnapshot{
		token:      s.poeOAuth.token,
		inProgress: s.poeOAuth.inProgress,
		lastError:  s.poeOAuth.lastError,
	}
}

func (s *server) publishPoeOAuthStatus() {
	msg, _ := json.Marshal(proto.Message{
		Type:    proto.TypeEvent,
		Topic:   proto.TopicPoeOAuthStatus,
		Payload: mustMarshal(s.poeOAuthSnapshot().statusPayload(time.Now())),
	})
	s.hub.Publish(proto.TopicPoeOAuthStatus, msg)
}

// loadPoeOAuthToken, savePoeOAuthToken, and clearPoeOAuthToken are the sole
// points this package touches internal/creds for PoE OAuth — kept thin and
// untested here, mirroring handleCredentialsStore/Has/Delete's existing
// convention of not exercising the real OS credential store from this
// package's automated tests (see poe-info-service/CONTRIBUTING.md).
func loadPoeOAuthToken() (poe.Token, bool) {
	raw, err := creds.Get(creds.ServiceName, poeOAuthCredKey)
	if err != nil {
		return poe.Token{}, false
	}
	var tok poe.Token
	if err := json.Unmarshal([]byte(raw), &tok); err != nil {
		log.Printf("poe oauth: stored token is corrupt, discarding: %v", err)
		return poe.Token{}, false
	}
	return tok, true
}

func savePoeOAuthToken(tok poe.Token) error {
	raw, err := json.Marshal(tok)
	if err != nil {
		return err
	}
	return creds.Store(creds.ServiceName, poeOAuthCredKey, string(raw))
}

func clearPoeOAuthToken() error {
	return creds.Delete(creds.ServiceName, poeOAuthCredKey)
}

// upsertOAuthAccount records (or refreshes) the accounts row for an
// OAuth-authenticated PoE account, keyed by name — the same ON CONFLICT(name)
// convention internal/ingest/writer.go already uses for accounts rows
// sourced from Client.txt guild events, so an account seen both ways
// collapses onto one row rather than duplicating. oauth_authenticated_at
// doubles as the "this is the currently signed-in account" flag (non-NULL)
// and a record of when, mirroring the ended_at/exited_at convention used
// elsewhere in the schema. oauth_credential_key is only the fixed
// poeOAuthCredKey constant today (one global credential per ADR-005), but the
// column exists so a future per-account key (see ROADMAP_DETAILS.md's
// "Multi-account PoE OAuth support" entry) needs no further schema change.
func upsertOAuthAccount(db *sql.DB, tok poe.Token) error {
	_, err := db.Exec(
		`INSERT INTO accounts(name, poe_uuid, oauth_credential_key, oauth_authenticated_at)
		 VALUES (?, ?, ?, ?)
		 ON CONFLICT(name) DO UPDATE SET
		   poe_uuid = excluded.poe_uuid,
		   oauth_credential_key = excluded.oauth_credential_key,
		   oauth_authenticated_at = excluded.oauth_authenticated_at`,
		tok.Username, tok.Sub, poeOAuthCredKey, time.Now().UTC().Format(time.RFC3339))
	return err
}

// clearOAuthAccountActive marks the account identified by sub (the token's
// `sub` claim) as no longer the signed-in PoE OAuth account, without
// deleting the row — name/poe_uuid survive as a historical record. Called on
// logout; a no-op if sub is empty (nothing was ever authenticated).
func clearOAuthAccountActive(db *sql.DB, sub string) error {
	if sub == "" {
		return nil
	}
	_, err := db.Exec(
		`UPDATE accounts SET oauth_credential_key = NULL, oauth_authenticated_at = NULL WHERE poe_uuid = ?`,
		sub)
	return err
}

// hydratePoeOAuthToken loads any previously persisted token at startup and
// schedules its next refresh, discarding it outright if it's already past
// the assumed refresh-token lifetime (poe-apis.md §3.3's "On startup"
// state machine) — a token that can't refresh is not worth surfacing as
// authorized only to fail on first use.
func (s *server) hydratePoeOAuthToken() {
	tok, ok := loadPoeOAuthToken()
	if !ok {
		return
	}
	if tok.PastRefreshWindow(time.Now()) {
		clearPoeOAuthToken()
		return
	}
	s.poeOAuth.mu.Lock()
	s.poeOAuth.token = &tok
	s.poeOAuth.mu.Unlock()
	s.schedulePoeOAuthRefresh(tok)
}

// schedulePoeOAuthRefresh (re)arms the refresh timer for refreshEarlyWindow
// before tok's access token expires — immediately if that time has already
// passed, matching poe-apis.md's "else if within 300s of expiry -> refresh
// now" startup rule via time.AfterFunc's zero/negative-duration behavior.
func (s *server) schedulePoeOAuthRefresh(tok poe.Token) {
	delay := time.Until(tok.RefreshAt())
	if delay < 0 {
		delay = 0
	}
	s.poeOAuth.mu.Lock()
	if s.poeOAuth.refreshTimer != nil {
		s.poeOAuth.refreshTimer.Stop()
	}
	s.poeOAuth.refreshTimer = time.AfterFunc(delay, s.runPoeOAuthRefresh)
	s.poeOAuth.mu.Unlock()
}

// runPoeOAuthRefresh performs one background token refresh. On success, the
// new token is persisted and the next refresh scheduled. On failure it
// retries after poeOAuthRefreshRetryBackoff rather than immediately forcing
// re-login, unless the refresh-token lifetime has now elapsed entirely, in
// which case the token is dropped and the user must sign in again.
func (s *server) runPoeOAuthRefresh() {
	s.poeOAuth.mu.Lock()
	tok := s.poeOAuth.token
	s.poeOAuth.mu.Unlock()
	if tok == nil {
		return
	}

	now := time.Now()
	if tok.PastRefreshWindow(now) {
		s.poeOAuth.mu.Lock()
		s.poeOAuth.token = nil
		s.poeOAuth.lastError = "refresh token expired; sign in again"
		s.poeOAuth.mu.Unlock()
		clearPoeOAuthToken()
		s.publishPoeOAuthStatus()
		return
	}

	ctx, cancel := context.WithTimeout(s.rootCtx, 30*time.Second)
	defer cancel()
	newTok, err := s.poeClient.Refresh(ctx, tok.RefreshToken)
	if err != nil {
		log.Printf("poe oauth: background refresh failed, will retry: %v", err)
		s.poeOAuth.mu.Lock()
		s.poeOAuth.lastError = err.Error()
		s.poeOAuth.refreshTimer = time.AfterFunc(poeOAuthRefreshRetryBackoff, s.runPoeOAuthRefresh)
		s.poeOAuth.mu.Unlock()
		s.publishPoeOAuthStatus()
		return
	}

	if err := savePoeOAuthToken(newTok); err != nil {
		log.Printf("poe oauth: persisting refreshed token failed: %v", err)
	}
	if err := upsertOAuthAccount(s.db, newTok); err != nil {
		log.Printf("poe oauth: recording account failed: %v", err)
	}
	s.poeOAuth.mu.Lock()
	s.poeOAuth.token = &newTok
	s.poeOAuth.lastError = ""
	s.poeOAuth.mu.Unlock()
	s.publishPoeOAuthStatus()
	s.schedulePoeOAuthRefresh(newTok)
}

// handlePoeOAuthLogin starts an interactive login flow: the response
// carries only whether the flow was actually (re)started, never the
// eventual result — a client learns the outcome from TopicPoeOAuthStatus
// (or a follow-up poe.oauth.status call). A login already in flight is left
// running rather than started twice.
func (s *server) handlePoeOAuthLogin(c *hub.Client, msg proto.Message) {
	s.poeOAuth.mu.Lock()
	if s.poeOAuth.inProgress {
		s.poeOAuth.mu.Unlock()
		s.send(c, proto.Message{
			Type:    proto.TypeResponse,
			ID:      msg.ID,
			Payload: mustMarshal(map[string]bool{"started": false}),
		})
		return
	}
	s.poeOAuth.inProgress = true
	s.poeOAuth.lastError = ""
	s.poeOAuth.mu.Unlock()

	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"started": true}),
	})
	s.publishPoeOAuthStatus()

	go s.runPoeOAuthLogin()
}

func (s *server) runPoeOAuthLogin() {
	tok, err := poe.Login(s.rootCtx, s.poeClient, poe.LoginOptions{})

	s.poeOAuth.mu.Lock()
	s.poeOAuth.inProgress = false
	s.poeOAuth.mu.Unlock()

	if err != nil {
		log.Printf("poe oauth: login failed: %v", err)
		s.poeOAuth.mu.Lock()
		s.poeOAuth.lastError = err.Error()
		s.poeOAuth.mu.Unlock()
		s.publishPoeOAuthStatus()
		return
	}

	if err := savePoeOAuthToken(tok); err != nil {
		log.Printf("poe oauth: persisting token failed: %v", err)
	}
	if err := upsertOAuthAccount(s.db, tok); err != nil {
		log.Printf("poe oauth: recording account failed: %v", err)
	}
	s.poeOAuth.mu.Lock()
	s.poeOAuth.token = &tok
	s.poeOAuth.lastError = ""
	s.poeOAuth.mu.Unlock()
	s.publishPoeOAuthStatus()
	s.schedulePoeOAuthRefresh(tok)
}

func (s *server) handlePoeOAuthStatus(c *hub.Client, msg proto.Message) {
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(s.poeOAuthSnapshot().statusPayload(time.Now())),
	})
}

// handlePoeOAuthLogout discards the in-memory and persisted token and
// cancels any pending refresh. Per ADR-004 there is nothing to hand back to
// the client — only confirmation that it's gone.
func (s *server) handlePoeOAuthLogout(c *hub.Client, msg proto.Message) {
	s.poeOAuth.mu.Lock()
	if s.poeOAuth.refreshTimer != nil {
		s.poeOAuth.refreshTimer.Stop()
		s.poeOAuth.refreshTimer = nil
	}
	var sub string
	if s.poeOAuth.token != nil {
		sub = s.poeOAuth.token.Sub
	}
	s.poeOAuth.token = nil
	s.poeOAuth.lastError = ""
	s.poeOAuth.mu.Unlock()

	if err := clearPoeOAuthToken(); err != nil {
		log.Printf("poe oauth: logout credential delete failed: %v", err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	if err := clearOAuthAccountActive(s.db, sub); err != nil {
		log.Printf("poe oauth: clearing account active state failed: %v", err)
	}

	s.publishPoeOAuthStatus()
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
	})
}

// fetchPoeAccounts returns every row of accounts as a PoeAccountSummary,
// ordered by name. PoeUUID is empty for an account never seen via OAuth
// login (e.g. a friend's account known only from Client.txt guild events);
// Active reflects oauth_authenticated_at, non-NULL for exactly the one
// account (if any) currently signed in via PoE OAuth on this service.
func fetchPoeAccounts(db *sql.DB) ([]proto.PoeAccountSummary, error) {
	rows, err := db.Query(`
		SELECT name, COALESCE(poe_uuid, ''), oauth_authenticated_at IS NOT NULL
		FROM accounts
		ORDER BY name`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	accounts := []proto.PoeAccountSummary{}
	for rows.Next() {
		var a proto.PoeAccountSummary
		if err := rows.Scan(&a.Name, &a.PoeUUID, &a.Active); err != nil {
			return nil, err
		}
		accounts = append(accounts, a)
	}
	return accounts, rows.Err()
}

// handlePoeAccountsList serves "poe.accounts.list" — every account this
// service knows of, letting a client (e.g. l2p-poe) decide whether to show
// an account switcher at all: only once this list has more than one entry.
func (s *server) handlePoeAccountsList(c *hub.Client, msg proto.Message) {
	if s.db == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	accounts, err := fetchPoeAccounts(s.db)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"accounts": accounts}),
	})
}
