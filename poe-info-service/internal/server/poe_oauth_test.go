package server

import (
	"database/sql"
	"encoding/json"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/schema"
)

// openAccountsTestDB returns an in-memory database with the real schema
// applied — upsertOAuthAccount/clearOAuthAccountActive only ever touch
// poe-info-service's own SQLite database (never the OS credential store),
// so unlike the credential-store functions covered by this file's top
// comment, these are safe and expected to be exercised directly.
func openAccountsTestDB(t *testing.T) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite", "file:"+t.Name()+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	if err := schema.EnsureSchema(db); err != nil {
		t.Fatalf("ensure schema: %v", err)
	}
	return db
}

// Tests below exercise poe_oauth.go's in-memory logic only — never
// loadPoeOAuthToken/savePoeOAuthToken/clearPoeOAuthToken, which are the sole
// points this package touches the real OS credential store. That mirrors
// this package's existing convention of leaving the credentials.store/
// has/delete handlers themselves untested here (see CONTRIBUTING.md's
// "Windows Credential Manager during tests" note) — automated tests must
// never risk deleting or depending on a real stored credential.

func TestPoeOAuthStatusPayload_NoToken_Unauthorized(t *testing.T) {
	snap := poeOAuthSnapshot{}
	payload := snap.statusPayload(time.Now())

	if payload.Authorized {
		t.Error("Authorized = true with no token, want false")
	}
	if payload.Username != "" || payload.Scope != "" || payload.AccessExpiration != 0 {
		t.Errorf("expected no token metadata with no token, got %+v", payload)
	}
}

func TestPoeOAuthStatusPayload_ValidToken_Authorized(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	tok := &poe.Token{
		Username:          "SomeAccount",
		Scope:             "account:leagues",
		AccessExpiration:  now.Unix() + 3600,
		RefreshExpiration: now.Unix() + int64(7*24*time.Hour/time.Second),
	}
	snap := poeOAuthSnapshot{token: tok}
	payload := snap.statusPayload(now)

	if !payload.Authorized {
		t.Fatal("Authorized = false with a valid token, want true")
	}
	if payload.Username != "SomeAccount" {
		t.Errorf("Username = %q, want SomeAccount", payload.Username)
	}
	if payload.Scope != "account:leagues" {
		t.Errorf("Scope = %q, want account:leagues", payload.Scope)
	}
	if payload.AccessExpiration != tok.AccessExpiration {
		t.Errorf("AccessExpiration = %d, want %d", payload.AccessExpiration, tok.AccessExpiration)
	}
}

// TestPoeOAuthStatusPayload_TokenPastRefreshWindow_Unauthorized proves a
// token that has outlived the assumed 7-day refresh-token lifetime reports
// as unauthorized (re-login required) even though a Token struct is still
// present — Authorized reflects whether the token is actually still usable,
// not merely whether one was ever obtained.
func TestPoeOAuthStatusPayload_TokenPastRefreshWindow_Unauthorized(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	tok := &poe.Token{
		Username:          "StaleAccount",
		RefreshExpiration: now.Unix() - 1, // already expired
	}
	snap := poeOAuthSnapshot{token: tok}
	payload := snap.statusPayload(now)

	if payload.Authorized {
		t.Error("Authorized = true for a token past its refresh window, want false")
	}
	if payload.Username != "" {
		t.Errorf("Username = %q for an unauthorized status, want empty", payload.Username)
	}
}

func TestPoeOAuthStatusPayload_PassesThroughInProgressAndError(t *testing.T) {
	snap := poeOAuthSnapshot{inProgress: true, lastError: "network error"}
	payload := snap.statusPayload(time.Now())

	if !payload.InProgress {
		t.Error("InProgress = false, want true")
	}
	if payload.Error != "network error" {
		t.Errorf("Error = %q, want %q", payload.Error, "network error")
	}
}

// TestSchedulePoeOAuthRefresh_ReplacesExistingTimer proves a second call
// stops the first timer rather than leaving two scheduled refreshes racing
// each other (e.g. a login immediately followed by a startup hydration, or
// two logins in quick succession).
func TestSchedulePoeOAuthRefresh_ReplacesExistingTimer(t *testing.T) {
	srv := &server{}
	farFuture := time.Now().Add(24 * time.Hour).Unix()
	tok := poe.Token{AccessExpiration: farFuture}

	srv.schedulePoeOAuthRefresh(tok)
	first := srv.poeOAuth.refreshTimer
	if first == nil {
		t.Fatal("schedulePoeOAuthRefresh did not set a timer")
	}

	srv.schedulePoeOAuthRefresh(tok)
	second := srv.poeOAuth.refreshTimer
	if second == nil {
		t.Fatal("second schedulePoeOAuthRefresh did not set a timer")
	}
	if first == second {
		t.Error("second schedulePoeOAuthRefresh reused the same timer instead of replacing it")
	}
	// first should have been stopped by the second call — Stop returns
	// false if it already fired/was already stopped.
	if first.Stop() {
		t.Error("first timer was still active after being superseded")
	}

	t.Cleanup(func() { second.Stop() })
}

// TestHandlePoeOAuthLogin_AlreadyInProgress_DoesNotRestart proves a second
// login request while one is already running responds with started=false
// and leaves the in-flight flow's state untouched, rather than kicking off
// a second concurrent poe.Login (which would touch the real credential
// store on success — see this file's top comment for why that's avoided).
func TestHandlePoeOAuthLogin_AlreadyInProgress_DoesNotRestart(t *testing.T) {
	srv := &server{hub: hub.New()}
	srv.poeOAuth.inProgress = true

	c := hub.NewClient()
	defer c.Close()

	srv.handlePoeOAuthLogin(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Method: "poe.oauth.login"})

	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		var payload struct {
			Started bool `json:"started"`
		}
		if err := json.Unmarshal(resp.Payload, &payload); err != nil {
			t.Fatalf("unmarshal payload: %v", err)
		}
		if payload.Started {
			t.Error("started = true while a login was already in progress, want false")
		}
	default:
		t.Fatal("handlePoeOAuthLogin sent no response")
	}

	if !srv.poeOAuth.inProgress {
		t.Error("inProgress flipped to false by the rejected second call")
	}
}

// TestHandlePoeOAuthStatus_ReflectsInMemoryState proves the poe.oauth.status
// handler serves straight from in-memory state (never touching the
// credential store itself — only the login/refresh/logout paths that
// populate that state do).
func TestHandlePoeOAuthStatus_ReflectsInMemoryState(t *testing.T) {
	srv := &server{hub: hub.New()}
	now := time.Now()
	srv.poeOAuth.token = &poe.Token{
		Username:          "SomeAccount",
		AccessExpiration:  now.Add(time.Hour).Unix(),
		RefreshExpiration: now.Add(24 * time.Hour).Unix(),
	}

	c := hub.NewClient()
	defer c.Close()

	srv.handlePoeOAuthStatus(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Method: "poe.oauth.status"})

	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		var payload proto.PoeOAuthStatusPayload
		if err := json.Unmarshal(resp.Payload, &payload); err != nil {
			t.Fatalf("unmarshal payload: %v", err)
		}
		if !payload.Authorized || payload.Username != "SomeAccount" {
			t.Errorf("status payload = %+v, want authorized=true username=SomeAccount", payload)
		}
	default:
		t.Fatal("handlePoeOAuthStatus sent no response")
	}
}

// TestUpsertOAuthAccount_InsertsNewAccount proves a first-time OAuth login
// creates an accounts row carrying the token's identity plus the
// credential-key/authenticated-at markers that indicate it's currently signed in.
func TestUpsertOAuthAccount_InsertsNewAccount(t *testing.T) {
	db := openAccountsTestDB(t)
	tok := poe.Token{Username: "SomeAccount", Sub: "uuid-1"}

	if err := upsertOAuthAccount(db, tok); err != nil {
		t.Fatalf("upsertOAuthAccount: %v", err)
	}

	var poeUUID, credKey, authedAt string
	err := db.QueryRow(`SELECT poe_uuid, oauth_credential_key, oauth_authenticated_at FROM accounts WHERE name = ?`, "SomeAccount").
		Scan(&poeUUID, &credKey, &authedAt)
	if err != nil {
		t.Fatalf("query account: %v", err)
	}
	if poeUUID != "uuid-1" {
		t.Errorf("poe_uuid = %q, want uuid-1", poeUUID)
	}
	if credKey != poeOAuthCredKey {
		t.Errorf("oauth_credential_key = %q, want %q", credKey, poeOAuthCredKey)
	}
	if authedAt == "" {
		t.Error("oauth_authenticated_at is empty, want a timestamp")
	}
}

// TestUpsertOAuthAccount_MergesWithExistingChatSeenAccount proves an account
// already known from Client.txt guild events (accounts.name populated with no
// OAuth data) picks up the OAuth columns on the same row rather than creating
// a duplicate — accounts.name is the shared key across both sources.
func TestUpsertOAuthAccount_MergesWithExistingChatSeenAccount(t *testing.T) {
	db := openAccountsTestDB(t)
	if _, err := db.Exec(`INSERT INTO accounts(name, guild_name) VALUES(?, ?)`, "SomeAccount", "MyGuild"); err != nil {
		t.Fatalf("seed chat-seen account: %v", err)
	}

	tok := poe.Token{Username: "SomeAccount", Sub: "uuid-1"}
	if err := upsertOAuthAccount(db, tok); err != nil {
		t.Fatalf("upsertOAuthAccount: %v", err)
	}

	var count int
	if err := db.QueryRow(`SELECT COUNT(*) FROM accounts WHERE name = ?`, "SomeAccount").Scan(&count); err != nil {
		t.Fatalf("count accounts: %v", err)
	}
	if count != 1 {
		t.Fatalf("got %d accounts rows named SomeAccount, want 1 (merged)", count)
	}

	var guildName, poeUUID string
	if err := db.QueryRow(`SELECT guild_name, poe_uuid FROM accounts WHERE name = ?`, "SomeAccount").Scan(&guildName, &poeUUID); err != nil {
		t.Fatalf("query merged account: %v", err)
	}
	if guildName != "MyGuild" {
		t.Errorf("guild_name = %q, want MyGuild to survive the merge", guildName)
	}
	if poeUUID != "uuid-1" {
		t.Errorf("poe_uuid = %q, want uuid-1", poeUUID)
	}
}

// TestClearOAuthAccountActive_ClearsCredentialAndTimestampOnly proves logout
// clears the "currently signed in" markers but leaves the account's identity
// (name, poe_uuid) in place as a historical record.
func TestClearOAuthAccountActive_ClearsCredentialAndTimestampOnly(t *testing.T) {
	db := openAccountsTestDB(t)
	tok := poe.Token{Username: "SomeAccount", Sub: "uuid-1"}
	if err := upsertOAuthAccount(db, tok); err != nil {
		t.Fatalf("upsertOAuthAccount: %v", err)
	}

	if err := clearOAuthAccountActive(db, "uuid-1"); err != nil {
		t.Fatalf("clearOAuthAccountActive: %v", err)
	}

	var poeUUID string
	var credKey, authedAt sql.NullString
	err := db.QueryRow(`SELECT poe_uuid, oauth_credential_key, oauth_authenticated_at FROM accounts WHERE name = ?`, "SomeAccount").
		Scan(&poeUUID, &credKey, &authedAt)
	if err != nil {
		t.Fatalf("query account: %v", err)
	}
	if poeUUID != "uuid-1" {
		t.Errorf("poe_uuid = %q, want uuid-1 to survive logout", poeUUID)
	}
	if credKey.Valid || authedAt.Valid {
		t.Errorf("oauth_credential_key/oauth_authenticated_at = %+v/%+v, want both NULL after logout", credKey, authedAt)
	}
}

// TestClearOAuthAccountActive_EmptySub_NoOp proves an empty sub (nothing was
// ever authenticated this run) does nothing rather than erroring or matching
// every row with a NULL poe_uuid.
func TestClearOAuthAccountActive_EmptySub_NoOp(t *testing.T) {
	db := openAccountsTestDB(t)
	if _, err := db.Exec(`INSERT INTO accounts(name) VALUES(?)`, "Unrelated"); err != nil {
		t.Fatalf("seed account: %v", err)
	}

	if err := clearOAuthAccountActive(db, ""); err != nil {
		t.Fatalf("clearOAuthAccountActive: %v", err)
	}

	var name string
	if err := db.QueryRow(`SELECT name FROM accounts WHERE name = ?`, "Unrelated").Scan(&name); err != nil {
		t.Fatalf("expected Unrelated account untouched: %v", err)
	}
}

// TestFetchPoeAccounts_MixOfChatSeenAndOAuthAccounts proves the list includes
// both an account known only from Client.txt guild events (empty PoeUUID,
// Active false) and one currently signed in via OAuth (PoeUUID populated,
// Active true), ordered by name.
func TestFetchPoeAccounts_MixOfChatSeenAndOAuthAccounts(t *testing.T) {
	db := openAccountsTestDB(t)
	if _, err := db.Exec(`INSERT INTO accounts(name, guild_name) VALUES(?, ?)`, "ZChatOnlyFriend", "MyGuild"); err != nil {
		t.Fatalf("seed chat-seen account: %v", err)
	}
	tok := poe.Token{Username: "ASignedInAccount", Sub: "uuid-1"}
	if err := upsertOAuthAccount(db, tok); err != nil {
		t.Fatalf("upsertOAuthAccount: %v", err)
	}

	accounts, err := fetchPoeAccounts(db)
	if err != nil {
		t.Fatalf("fetchPoeAccounts: %v", err)
	}
	if len(accounts) != 2 {
		t.Fatalf("got %d accounts, want 2: %+v", len(accounts), accounts)
	}

	// ORDER BY name: "ASignedInAccount" sorts before "ZChatOnlyFriend".
	signedIn, chatOnly := accounts[0], accounts[1]
	if signedIn.Name != "ASignedInAccount" || signedIn.PoeUUID != "uuid-1" || !signedIn.Active {
		t.Errorf("signed-in account = %+v, want {ASignedInAccount uuid-1 true}", signedIn)
	}
	if chatOnly.Name != "ZChatOnlyFriend" || chatOnly.PoeUUID != "" || chatOnly.Active {
		t.Errorf("chat-only account = %+v, want {ZChatOnlyFriend \"\" false}", chatOnly)
	}
}

// TestHandlePoeAccountsList_ReturnsAccountsFromDB proves the WS handler
// serves fetchPoeAccounts's result wrapped in the expected {"accounts": [...]}
// shape.
func TestHandlePoeAccountsList_ReturnsAccountsFromDB(t *testing.T) {
	db := openAccountsTestDB(t)
	tok := poe.Token{Username: "SomeAccount", Sub: "uuid-1"}
	if err := upsertOAuthAccount(db, tok); err != nil {
		t.Fatalf("upsertOAuthAccount: %v", err)
	}
	srv := &server{db: db}

	c := hub.NewClient()
	defer c.Close()
	srv.handlePoeAccountsList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Method: "poe.accounts.list"})

	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		if resp.Error != "" {
			t.Fatalf("handlePoeAccountsList returned error: %s", resp.Error)
		}
		var payload struct {
			Accounts []proto.PoeAccountSummary `json:"accounts"`
		}
		if err := json.Unmarshal(resp.Payload, &payload); err != nil {
			t.Fatalf("unmarshal payload: %v", err)
		}
		if len(payload.Accounts) != 1 || payload.Accounts[0].Name != "SomeAccount" ||
			payload.Accounts[0].PoeUUID != "uuid-1" || !payload.Accounts[0].Active {
			t.Errorf("accounts = %+v, want one active SomeAccount/uuid-1", payload.Accounts)
		}
	default:
		t.Fatal("handlePoeAccountsList sent no response")
	}
}

// TestHandlePoeAccountsList_NoDB_ReturnsError proves a server with no db
// configured reports an error rather than panicking.
func TestHandlePoeAccountsList_NoDB_ReturnsError(t *testing.T) {
	srv := &server{}
	c := hub.NewClient()
	defer c.Close()
	srv.handlePoeAccountsList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Method: "poe.accounts.list"})

	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		if resp.Error == "" {
			t.Error("expected an error with no db configured, got none")
		}
	default:
		t.Fatal("handlePoeAccountsList sent no response")
	}
}
