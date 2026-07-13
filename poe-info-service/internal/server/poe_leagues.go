package server

import (
	"context"
	"database/sql"
	"encoding/json"
	"net/http"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
)

// defaultLeaguesRealm/defaultLeaguesType mirror GET /leagues's own
// documented defaults (poe-apis.md §6.2) — used whenever a poe.leagues.list
// /poe.leagues.detail request omits realm/type.
const (
	defaultLeaguesRealm = "pc"
	defaultLeaguesType  = "main"
)

// leagueSelectColumns is shared by queryLeaguesRows and queryLeagueByName —
// see scanLeagueRow, which both feed through.
const leagueSelectColumns = `name, realm, COALESCE(url,''), COALESCE(start_at,''), COALESCE(end_at,''), COALESCE(description,''), rules_json, is_event, is_delve_event, fetched_at`

func leaguesFetchKey(realm, typ, season string) string {
	return "poe:leagues:" + realm + ":" + typ + ":" + season
}

// leaguesFetchResult is what a leagues fetch Task hands back through
// reqqueue.Waiter.Wait — the freshly upserted rows, the fetch's timestamp,
// and this specific call's FetchCost, so a wait:true caller (both
// poe.leagues.list and poe.leagues.detail, which projects one league back
// out of Leagues) doesn't need a second DB round-trip or to recompute cost.
type leaguesFetchResult struct {
	leagues   []proto.LeagueSummary
	fetchedAt time.Time
	cost      *proto.FetchCost
}

// upsertLeagues writes fetched into the leagues table, keyed by (name,
// realm) — a league already known from a prior fetch has its mutable fields
// (url/dates/description/rules/event flags) refreshed in place along with
// fetched_at, rather than accumulating duplicate rows.
func upsertLeagues(db *sql.DB, fetched []poe.League, fetchedAt time.Time) error {
	const stmt = `
		INSERT INTO leagues(name, realm, url, start_at, end_at, description, rules_json, is_event, is_delve_event, fetched_at)
		VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		ON CONFLICT(name, realm) DO UPDATE SET
		  url = excluded.url,
		  start_at = excluded.start_at,
		  end_at = excluded.end_at,
		  description = excluded.description,
		  rules_json = excluded.rules_json,
		  is_event = excluded.is_event,
		  is_delve_event = excluded.is_delve_event,
		  fetched_at = excluded.fetched_at`

	fetchedAtStr := fetchedAt.UTC().Format(time.RFC3339)
	for _, lg := range fetched {
		ruleIDs := make([]string, 0, len(lg.Rules))
		for _, r := range lg.Rules {
			ruleIDs = append(ruleIDs, r.ID)
		}
		rulesJSON, err := json.Marshal(ruleIDs)
		if err != nil {
			return err
		}
		isEvent, isDelveEvent := 0, 0
		if lg.Event {
			isEvent = 1
		}
		if lg.DelveEvent {
			isDelveEvent = 1
		}
		if _, err := db.Exec(stmt, lg.ID, lg.Realm, lg.URL, lg.StartAt, lg.EndAt, lg.Description,
			string(rulesJSON), isEvent, isDelveEvent, fetchedAtStr); err != nil {
			return err
		}
	}
	return nil
}

// leagueRowScanner is satisfied by both *sql.Row (queryLeagueByName) and
// *sql.Rows (queryLeaguesRows), letting scanLeagueRow serve both.
type leagueRowScanner interface {
	Scan(dest ...any) error
}

// scanLeagueRow scans one leagueSelectColumns row into a LeagueSummary plus
// its fetched_at as a time.Time (zero value if the stored string somehow
// doesn't parse — never written that way by upsertLeagues, so this should
// only ever happen against a hand-crafted test row).
func scanLeagueRow(row leagueRowScanner) (proto.LeagueSummary, time.Time, error) {
	var ls proto.LeagueSummary
	var rulesJSON, fetchedAtStr string
	var isEventInt, isDelveEventInt int
	if err := row.Scan(&ls.Name, &ls.Realm, &ls.URL, &ls.StartAt, &ls.EndAt, &ls.Description,
		&rulesJSON, &isEventInt, &isDelveEventInt, &fetchedAtStr); err != nil {
		return proto.LeagueSummary{}, time.Time{}, err
	}
	ls.Event = isEventInt != 0
	ls.DelveEvent = isDelveEventInt != 0
	if err := json.Unmarshal([]byte(rulesJSON), &ls.Rules); err != nil {
		return proto.LeagueSummary{}, time.Time{}, err
	}
	fetchedAt, _ := time.Parse(time.RFC3339, fetchedAtStr)
	return ls, fetchedAt, nil
}

// queryLeaguesRows returns every leagues row for realm — event leagues only
// when typ == "event", every non-event (permanent/challenge) league
// otherwise, matching GET /leagues's own type=main/type=event split
// (poe-apis.md §6.2) — ordered by name, plus the oldest matching fetched_at
// so ensureLeagues can tell whether the cached set is within a requested
// max-age (zero Time, per scanLeagueRow's caller loop below, when there are
// no rows at all — see TestQueryLeaguesRows_Empty_ZeroTimeOldest). A
// "season" typ is treated the same as "main" here: this table doesn't
// record which season a fetch used, so a season-scoped request can only
// ever gate on the same non-event rows a plain "main" request would.
func queryLeaguesRows(db *sql.DB, realm, typ string) ([]proto.LeagueSummary, time.Time, error) {
	query := `SELECT ` + leagueSelectColumns + ` FROM leagues WHERE realm = ? AND is_event = ? ORDER BY name`
	isEvent := 0
	if typ == "event" {
		isEvent = 1
	}

	rows, err := db.Query(query, realm, isEvent)
	if err != nil {
		return nil, time.Time{}, err
	}
	defer rows.Close()

	var out []proto.LeagueSummary
	var oldest time.Time
	for rows.Next() {
		ls, fetchedAt, err := scanLeagueRow(rows)
		if err != nil {
			return nil, time.Time{}, err
		}
		out = append(out, ls)
		if oldest.IsZero() || fetchedAt.Before(oldest) {
			oldest = fetchedAt
		}
	}
	return out, oldest, rows.Err()
}

// queryLeagueByName returns the single cached leagues-table row for
// name+realm (regardless of its is_event flag — a caller here already
// knows the exact name it wants, unlike queryLeaguesRows's type-filtered
// listing), and whether it exists at all. Shared by
// ensureLeagueDetail (poe.leagues.detail's cache check) and steam.go's
// zero-cost current-league enrichment.
func queryLeagueByName(db *sql.DB, name, realm string) (league proto.LeagueSummary, fetchedAt time.Time, haveCache bool, err error) {
	row := db.QueryRow(`SELECT `+leagueSelectColumns+` FROM leagues WHERE name = ? AND realm = ?`, name, realm)
	ls, fa, scanErr := scanLeagueRow(row)
	if scanErr == sql.ErrNoRows {
		return proto.LeagueSummary{}, time.Time{}, false, nil
	}
	if scanErr != nil {
		return proto.LeagueSummary{}, time.Time{}, false, scanErr
	}
	return ls, fa, true, nil
}

// leagueDetailFor looks up name (typically the player's *current* league,
// parsed for free from Steam rich presence — see steam.go's poe.league/
// TopicLeague) against the leagues table's cached rows, assuming the pc
// realm (Steam-based rich presence only ever describes a pc client). A
// zero-cost enrichment: a plain DB read, never a PoE OAuth API call, and
// never triggers a fetch — nil if name is empty, no db is configured, or
// nothing is cached for it yet (e.g. poe.leagues.list/.detail was never
// called for this realm).
func (s *server) leagueDetailFor(name string) *proto.LeagueSummary {
	if name == "" || s.db == nil {
		return nil
	}
	ls, _, ok, err := queryLeagueByName(s.db, name, defaultLeaguesRealm)
	if err != nil || !ok {
		return nil
	}
	return &ls
}

// submitLeaguesFetch enqueues (de-duplicated by realm/typ/season, via
// reqqueue's Key-based merge) a /leagues fetch at the given priority
// through s.poeQueue, returning the resulting Waiter — shared by
// ensureLeagues (a bulk poe.leagues.list request) and ensureLeagueDetail (a
// single-league poe.leagues.detail request, which has no dedicated
// single-league endpoint of its own to call — see
// handlePoeLeaguesDetail's doc comment — so it just shares this same bulk
// fetch and its dedup key, meaning a concurrent list+detail request for the
// same realm/typ collapses into one fetch rather than two).
func (s *server) submitLeaguesFetch(realm, typ, season string, priority int) *reqqueue.Waiter {
	w := s.poeQueue.Submit(reqqueue.Task{
		Key:        leaguesFetchKey(realm, typ, season),
		Priority:   priority,
		PolicyHint: poeOAuthLeaguesPolicyHint,
		Exec: func(ctx context.Context) (any, http.Header, error) {
			fetched, headers, err := s.poeClient.FetchLeagues(ctx, poe.LeaguesParams{Realm: realm, Type: typ, Season: season})
			cost := buildFetchCost(headers)
			if err != nil {
				s.publishPoeLeaguesError(err, cost)
				return nil, headers, err
			}
			now := time.Now()
			if err := upsertLeagues(s.db, fetched, now); err != nil {
				s.publishPoeLeaguesError(err, cost)
				return nil, headers, err
			}
			result, _, err := queryLeaguesRows(s.db, realm, typ)
			if err != nil {
				s.publishPoeLeaguesError(err, cost)
				return nil, headers, err
			}
			s.publishPoeLeagues(result, now, cost)
			return leaguesFetchResult{leagues: result, fetchedAt: now, cost: cost}, headers, nil
		},
	})
	s.publishPoeRateLimitStatusAfter(w, poeLeaguesWaitTimeout)
	return w
}

// ensureLeagues returns the cached leagues table rows for realm/typ
// (haveCache/isFresh/fetchedAt describe them — see freshnessLabel) and,
// depending on fetchPolicy, may also enqueue a fetch through
// submitLeaguesFetch — see ensurePoeProfile's doc comment for the shared
// "never"/"ifStale"/"always" vocabulary; unlike ensurePoeProfile, this never
// needs an access token — GET /leagues is public — so a needed fetch is
// always schedulable regardless of PoE OAuth sign-in state.
func (s *server) ensureLeagues(realm, typ, season string, maxAge time.Duration, priority int, fetchPolicy string) (cached []proto.LeagueSummary, haveCache bool, isFresh bool, fetchedAt time.Time, waiter *reqqueue.Waiter) {
	rows, oldest, err := queryLeaguesRows(s.db, realm, typ)
	haveCache = err == nil && len(rows) > 0
	isFresh = haveCache && time.Since(oldest) < maxAge

	needFetch := fetchPolicy == fetchPolicyAlways || (!isFresh && fetchPolicy != fetchPolicyNever)
	if !needFetch {
		return rows, haveCache, isFresh, oldest, nil
	}
	return rows, haveCache, isFresh, oldest, s.submitLeaguesFetch(realm, typ, season, priority)
}

// ensureLeagueDetail is ensureLeagues's single-league counterpart, for
// poe.leagues.detail — see submitLeaguesFetch's doc comment for why a
// needed fetch here still goes through the same bulk mechanism.
func (s *server) ensureLeagueDetail(name, realm, typ string, maxAge time.Duration, priority int, fetchPolicy string) (cached proto.LeagueSummary, fetchedAt time.Time, haveCache bool, isFresh bool, waiter *reqqueue.Waiter) {
	row, fa, ok, err := queryLeagueByName(s.db, name, realm)
	haveCache = err == nil && ok
	isFresh = haveCache && time.Since(fa) < maxAge

	needFetch := fetchPolicy == fetchPolicyAlways || (!isFresh && fetchPolicy != fetchPolicyNever)
	if !needFetch {
		return row, fa, haveCache, isFresh, nil
	}
	return row, fa, haveCache, isFresh, s.submitLeaguesFetch(realm, typ, "", priority)
}

func (s *server) publishPoeLeagues(leagues []proto.LeagueSummary, fetchedAt time.Time, cost *proto.FetchCost) {
	msg, _ := json.Marshal(proto.Message{
		Type:  proto.TypeEvent,
		Topic: proto.TopicPoeLeagues,
		Payload: mustMarshal(proto.PoeLeaguesPayload{
			Status:    "ok",
			Freshness: "fresh",
			Leagues:   leagues,
			FetchedAt: fetchedAt.Unix(),
			Cost:      cost,
		}),
	})
	s.hub.Publish(proto.TopicPoeLeagues, msg)
}

func (s *server) publishPoeLeaguesError(fetchErr error, cost *proto.FetchCost) {
	msg, _ := json.Marshal(proto.Message{
		Type:  proto.TypeEvent,
		Topic: proto.TopicPoeLeagues,
		Payload: mustMarshal(proto.PoeLeaguesPayload{
			Status: "error",
			Error:  fetchErr.Error(),
			Cost:   cost,
		}),
	})
	s.hub.Publish(proto.TopicPoeLeagues, msg)
}

// poeLeaguesRequest is "poe.leagues.list"'s request shape. Realm/Type/Season
// mirror GET /leagues's own optional query parameters (poe-apis.md §6.2),
// defaulting to defaultLeaguesRealm/defaultLeaguesType when omitted.
// MaxAgeSeconds/Priority/Wait/Fetch/IncludeCost behave exactly like
// poeProfileFieldRequest's fields of the same name.
type poeLeaguesRequest struct {
	Realm         string `json:"realm"`
	Type          string `json:"type"`
	Season        string `json:"season"`
	MaxAgeSeconds int64  `json:"maxAgeSeconds"`
	Priority      int    `json:"priority"`
	Wait          bool   `json:"wait"`
	Fetch         string `json:"fetch"`
	IncludeCost   bool   `json:"includeCost"`
}

// handlePoeLeaguesList serves "poe.leagues.list" — the leagues table's
// current contents for the requested realm/type, refetched from GET
// /leagues through s.poeQueue whenever fetchPolicy allows and the cache is
// stale or empty. Follows handlePoeProfileField's freshness/fetching/cost
// conventions (see its doc comment) — minus the "not authenticated" error
// case, since /leagues is public and a fetch is always schedulable.
func (s *server) handlePoeLeaguesList(c *hub.Client, msg proto.Message) {
	var params poeLeaguesRequest
	if len(msg.Payload) > 0 {
		if err := json.Unmarshal(msg.Payload, &params); err != nil {
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
			return
		}
	}
	if s.db == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	fetchPolicy, err := normalizeFetchPolicy(params.Fetch)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}

	realm := params.Realm
	if realm == "" {
		realm = defaultLeaguesRealm
	}
	typ := params.Type
	if typ == "" {
		typ = defaultLeaguesType
	}

	maxAge := poeLeaguesCacheTTL
	if params.MaxAgeSeconds > 0 {
		maxAge = time.Duration(params.MaxAgeSeconds) * time.Second
	}
	if maxAge < poeLeaguesMinRefetchAge {
		maxAge = poeLeaguesMinRefetchAge
	}

	priority := poeLeaguesFetchPriority
	if params.Priority != 0 {
		priority = params.Priority
	}

	leagues, haveCache, isFresh, fetchedAt, waiter := s.ensureLeagues(realm, typ, params.Season, maxAge, priority, fetchPolicy)
	freshness := freshnessLabel(haveCache, isFresh)

	if waiter == nil {
		payload := proto.PoeLeaguesPayload{Status: freshness, Freshness: freshness, Leagues: leagues}
		if haveCache {
			payload.FetchedAt = fetchedAt.Unix()
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
		return
	}

	if !params.Wait {
		payload := proto.PoeLeaguesPayload{Status: "pending", Freshness: freshness, Fetching: true, Leagues: leagues}
		if haveCache {
			payload.FetchedAt = fetchedAt.Unix()
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
		return
	}

	go func() {
		ctx, cancel := context.WithTimeout(s.rootCtx, poeLeaguesWaitTimeout)
		defer cancel()
		result, err := waiter.Wait(ctx)
		if err != nil {
			payload := proto.PoeLeaguesPayload{Status: "pending", Freshness: freshness, Fetching: true, Leagues: leagues, Error: err.Error()}
			if haveCache {
				payload.FetchedAt = fetchedAt.Unix()
			}
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
			return
		}
		fetched := result.(leaguesFetchResult)
		payload := proto.PoeLeaguesPayload{Status: "ok", Freshness: "fresh", Leagues: fetched.leagues, FetchedAt: fetched.fetchedAt.Unix()}
		if params.IncludeCost {
			payload.Cost = fetched.cost
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
	}()
}

// poeLeagueDetailRequest is "poe.leagues.detail"'s request shape. Name is
// required — the exact league name (LeagueSummary.Name/the API's "id"
// field), not a display label. Realm/Type/MaxAgeSeconds/Priority/Wait/
// Fetch/IncludeCost behave like poeLeaguesRequest's fields of the same
// name; Type matters here because a needed fetch still has to ask GET
// /leagues for the right type=main/event bucket to find Name in (see
// ensureLeagueDetail) — there's no Season field, since a season-scoped
// single-league lookup isn't a scenario this service has a client for yet.
type poeLeagueDetailRequest struct {
	Name          string `json:"name"`
	Realm         string `json:"realm"`
	Type          string `json:"type"`
	MaxAgeSeconds int64  `json:"maxAgeSeconds"`
	Priority      int    `json:"priority"`
	Wait          bool   `json:"wait"`
	Fetch         string `json:"fetch"`
	IncludeCost   bool   `json:"includeCost"`
}

// handlePoeLeaguesDetail serves "poe.leagues.detail": one specific league's
// cached row, by name. There is no dedicated single-league endpoint in the
// PoE OAuth API today — only the bulk GET /leagues that poe.leagues.list
// already uses (poe-apis.md §6.2 lists only that one; its Legacy API has a
// single-league equivalent, `GET /api/leagues/{id}` — see §"Leagues
// (expanded)" — but the OAuth API doesn't mirror it). A "refresh" here
// therefore just re-runs that same bulk fetch (see submitLeaguesFetch) and
// projects the one requested league back out of it — sharing
// poe.leagues.list's reqqueue dedup key, so the two methods never
// double-fetch when called concurrently for the same realm/type. The
// request/response shape is deliberately independent of this
// implementation detail, so a future dedicated single-league OAuth endpoint
// could swap in behind submitLeaguesFetch with no wire-visible change. A
// non-blocking (wait:false) caller learns a refresh's outcome the same way
// poe.leagues.list's does: by subscribing to TopicPoeLeagues and filtering
// its Leagues array for Name — there's no dedicated topic for this method.
func (s *server) handlePoeLeaguesDetail(c *hub.Client, msg proto.Message) {
	var params poeLeagueDetailRequest
	if len(msg.Payload) > 0 {
		if err := json.Unmarshal(msg.Payload, &params); err != nil {
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
			return
		}
	}
	if params.Name == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: name required"})
		return
	}
	if s.db == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	fetchPolicy, err := normalizeFetchPolicy(params.Fetch)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}

	realm := params.Realm
	if realm == "" {
		realm = defaultLeaguesRealm
	}
	typ := params.Type
	if typ == "" {
		typ = defaultLeaguesType
	}

	maxAge := poeLeaguesCacheTTL
	if params.MaxAgeSeconds > 0 {
		maxAge = time.Duration(params.MaxAgeSeconds) * time.Second
	}
	if maxAge < poeLeaguesMinRefetchAge {
		maxAge = poeLeaguesMinRefetchAge
	}

	priority := poeLeaguesFetchPriority
	if params.Priority != 0 {
		priority = params.Priority
	}

	league, fetchedAt, haveCache, isFresh, waiter := s.ensureLeagueDetail(params.Name, realm, typ, maxAge, priority, fetchPolicy)
	freshness := freshnessLabel(haveCache, isFresh)

	if waiter == nil {
		payload := proto.PoeLeagueDetailPayload{Status: freshness, Freshness: freshness}
		if haveCache {
			payload.League = &league
			payload.FetchedAt = fetchedAt.Unix()
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
		return
	}

	if !params.Wait {
		payload := proto.PoeLeagueDetailPayload{Status: "pending", Freshness: freshness, Fetching: true}
		if haveCache {
			payload.League = &league
			payload.FetchedAt = fetchedAt.Unix()
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
		return
	}

	go func() {
		ctx, cancel := context.WithTimeout(s.rootCtx, poeLeaguesWaitTimeout)
		defer cancel()
		result, err := waiter.Wait(ctx)
		if err != nil {
			payload := proto.PoeLeagueDetailPayload{Status: "pending", Freshness: freshness, Fetching: true, Error: err.Error()}
			if haveCache {
				payload.League = &league
				payload.FetchedAt = fetchedAt.Unix()
			}
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
			return
		}
		fetched := result.(leaguesFetchResult)
		var found *proto.LeagueSummary
		for i := range fetched.leagues {
			if fetched.leagues[i].Name == params.Name {
				found = &fetched.leagues[i]
				break
			}
		}
		payload := proto.PoeLeagueDetailPayload{Status: "ok", Freshness: "fresh"}
		if found != nil {
			payload.League = found
			payload.FetchedAt = fetched.fetchedAt.Unix()
			if params.IncludeCost {
				payload.Cost = fetched.cost
			}
		} else {
			// The refresh completed, but Name wasn't in the returned set
			// (wrong realm/type, the league ended/renamed, or it was never
			// real) — a clean miss, not an error.
			payload.Status = "miss"
			payload.Freshness = "miss"
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
	}()
}
