// Package ingest applies parsed Client.txt events to the l2p database. It is
// a port of the write side of the old C++ LogIngestWorker: the parsing lives
// in internal/parser, and this package owns everything that used to be
// sqlite3_prepare_v2/bind/step calls in that file.
package ingest

import (
	"database/sql"
	"fmt"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/proto"
)

// Writer applies a stream of proto.ParsedEvent to the l2p database, one
// install's worth at a time. It carries the same in-memory session/span/
// character/guild state LogIngestWorker used to track locally while scanning
// Client.txt line by line.
type Writer struct {
	db        *sql.DB
	installID int64

	sessionID           int64
	sessionStartTs      string
	sessionAfkSecs      int64
	afkOnTs             string
	altTabOutTs         string
	sessionCharID       int64
	sessionCharLevel    int
	sessionAreaID       int64
	currentSpanID       int64
	currentSpanAfkSecs  int64
	lastPvpQueueEventID int64
	currentGuild        string
	lastTs              string
}

// NewWriter constructs a Writer for the given install, recovering an
// in-progress session/span if one is still open in the database (e.g. the
// service restarted while the game kept running).
func NewWriter(db *sql.DB, installID int64) (*Writer, error) {
	w := &Writer{
		db:                  db,
		installID:           installID,
		sessionID:           -1,
		sessionCharID:       -1,
		sessionCharLevel:    -1,
		sessionAreaID:       -1,
		currentSpanID:       -1,
		lastPvpQueueEventID: -1,
	}

	var sessID, spanID sql.NullInt64
	err := db.QueryRow(`
		SELECT s.id, ats.id
		FROM sessions s
		LEFT JOIN area_time_spans ats
		  ON ats.session_id = s.id AND ats.exited_at IS NULL
		WHERE s.install_id = ? AND s.ended_at IS NULL
		ORDER BY s.started_at DESC, ats.entered_at DESC
		LIMIT 1`, installID).Scan(&sessID, &spanID)
	if err != nil && err != sql.ErrNoRows {
		return nil, fmt.Errorf("recover open session: %w", err)
	}
	if sessID.Valid {
		w.sessionID = sessID.Int64
	}
	if spanID.Valid {
		w.currentSpanID = spanID.Int64
	}
	return w, nil
}

func nullIfNeg(v int64) any {
	if v < 0 {
		return nil
	}
	return v
}

func tsToSecs(ts string) int64 {
	t, err := time.Parse("2006-01-02 15:04:05", ts)
	if err != nil {
		return 0
	}
	return t.Unix()
}

func max0(v int64) int64 {
	if v < 0 {
		return 0
	}
	return v
}

// HandleEvent applies one parsed event to the database. If evt is broadcast
// to the "clientlog" hub topic afterwards (see server.go), any enrichment
// HandleEvent makes to evt.Data (e.g. area_type/area_subtype) is visible to
// that broadcast too, since Data is a shared map.
func (w *Writer) HandleEvent(evt proto.ParsedEvent) error {
	defer func() { w.lastTs = evt.Timestamp }()

	switch evt.Type {
	case proto.EventSessionStart:
		return w.handleSessionStart(evt.Timestamp)
	case proto.EventAreaEntered:
		return w.handleAreaEntered(evt)
	case proto.EventLevelUp:
		return w.handleLevelUp(evt)
	case proto.EventAfkOn:
		w.afkOnTs = evt.Timestamp
		return nil
	case proto.EventAfkOff:
		return w.handleAfkOff(evt.Timestamp)
	case proto.EventAltTabOut:
		return w.handleAltTabOut(evt.Timestamp)
	case proto.EventAltTabBack:
		return w.handleAltTabBack(evt.Timestamp)
	case proto.EventQuestEvent:
		return w.handleQuestEvent(evt)
	case proto.EventGeneralEvent:
		return w.handleGeneralEvent(evt)
	case proto.EventPlayed:
		return w.handlePlayed(evt)
	case proto.EventAchievement:
		return w.handleAchievement(evt)
	case proto.EventHideoutDiscovered:
		return w.handleHideoutDiscovered(evt)
	case proto.EventPvpQueue:
		return w.handlePvpQueue(evt)
	case proto.EventPvpQueueCancelled:
		return w.handlePvpQueueCancelled(evt.Timestamp)
	case proto.EventPassiveAllocated, proto.EventPassiveUnallocated:
		return w.handlePassiveAlloc(evt)
	case proto.EventWhisper:
		return w.handleWhisper(evt)
	case proto.EventChat:
		return w.handleChat(evt)
	case proto.EventCharacterDeath:
		return w.handleCharacterDeath(evt)
	case proto.EventGuildJoined:
		return w.handleGuildJoined(evt)
	case proto.EventGuildMemberUpdated:
		return w.handleGuildMemberUpdated(evt)
	case proto.EventChatChannelJoin:
		return w.handleChatChannelJoin(evt)
	case proto.EventPassivesSnapshot:
		return w.handlePassivesSnapshot(evt)
	case proto.EventLoginScreen:
		return w.handleClientScreenEvent(evt.Timestamp, "login_screen")
	case proto.EventCharSelect:
		return w.handleClientScreenEvent(evt.Timestamp, "char_select")
	}
	return nil
}

func str(data map[string]any, key string) string {
	v, _ := data[key].(string)
	return v
}

func intVal(data map[string]any, key string) int {
	switch v := data[key].(type) {
	case int:
		return v
	case int64:
		return int(v)
	case float64:
		return int(v)
	}
	return 0
}

func int64Val(data map[string]any, key string) int64 {
	switch v := data[key].(type) {
	case int:
		return int64(v)
	case int64:
		return v
	case float64:
		return int64(v)
	}
	return 0
}

func boolVal(data map[string]any, key string) bool {
	v, _ := data[key].(bool)
	return v
}

// insertEvent records an audit-log row in the events table pointing at the
// row res just inserted, but only if res actually inserted a new row (the
// caller's statement is INSERT OR IGNORE, so a no-op insert must not produce
// a phantom audit entry). Mirrors the insertEvent lambda in LogIngestWorker.
func (w *Writer) insertEvent(res sql.Result, ts, eventType string) error {
	n, err := res.RowsAffected()
	if err != nil || n == 0 {
		return nil
	}
	id, err := res.LastInsertId()
	if err != nil {
		return nil
	}
	_, err = w.db.Exec(`INSERT OR IGNORE INTO events(occurred_at, event_type, source_id) VALUES(?,?,?)`, ts, eventType, id)
	return err
}

// ── session / span lifecycle ─────────────────────────────────────────────

func (w *Writer) handleSessionStart(ts string) error {
	prevTs := w.lastTs
	if err := w.closeSession(prevTs); err != nil {
		return err
	}

	if _, err := w.db.Exec(`INSERT OR IGNORE INTO sessions(install_id, started_at) VALUES(?,?)`, w.installID, ts); err != nil {
		return err
	}
	w.sessionID = -1
	if err := w.db.QueryRow(`SELECT id FROM sessions WHERE install_id=? AND started_at=?`, w.installID, ts).Scan(&w.sessionID); err != nil {
		return err
	}

	w.sessionStartTs = ts
	w.sessionAfkSecs = 0
	w.sessionCharID = -1
	w.sessionCharLevel = -1
	w.sessionAreaID = -1

	return w.openSpan(ts, -1)
}

// ensureSession recovers or creates a session for ts, used when an
// area-entered event arrives with no session yet open (e.g. mid-file resume
// with no LOG FILE OPENING seen).
func (w *Writer) ensureSession(ts string) error {
	if w.sessionID >= 0 {
		return nil
	}

	var startedAt string
	err := w.db.QueryRow(
		`SELECT id, started_at FROM sessions WHERE install_id=? AND ended_at IS NULL ORDER BY started_at DESC LIMIT 1`,
		w.installID).Scan(&w.sessionID, &startedAt)
	if err == nil {
		w.sessionStartTs = startedAt
		return nil
	}
	if err != sql.ErrNoRows {
		return err
	}

	if _, err := w.db.Exec(`INSERT OR IGNORE INTO sessions(install_id, started_at) VALUES(?,?)`, w.installID, ts); err != nil {
		return err
	}
	w.sessionID = -1
	if err := w.db.QueryRow(`SELECT id FROM sessions WHERE install_id=? AND started_at=?`, w.installID, ts).Scan(&w.sessionID); err != nil {
		return err
	}
	w.sessionStartTs = ts
	w.sessionAfkSecs = 0
	w.sessionCharID = -1
	w.sessionCharLevel = -1
	w.sessionAreaID = -1
	return nil
}

func (w *Writer) closeSpan(endTs string) error {
	if w.currentSpanID < 0 || endTs == "" {
		return nil
	}
	if w.afkOnTs != "" {
		w.currentSpanAfkSecs += max0(tsToSecs(endTs) - tsToSecs(w.afkOnTs))
		w.afkOnTs = endTs
	}
	_, err := w.db.Exec(
		`UPDATE area_time_spans SET exited_at=?,
		    duration_secs=CAST((julianday(?)-julianday(entered_at))*86400.0 AS INTEGER),
		    afk_secs=?
		 WHERE id=?`,
		endTs, endTs, w.currentSpanAfkSecs, w.currentSpanID)
	w.currentSpanID = -1
	w.currentSpanAfkSecs = 0
	return err
}

func (w *Writer) openSpan(ts string, areaID int64) error {
	if w.sessionID < 0 {
		return nil
	}
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO area_time_spans(session_id, area_id, char_id, entered_at) VALUES(?,?,?,?)`,
		w.sessionID, nullIfNeg(areaID), nullIfNeg(w.sessionCharID), ts)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n > 0 {
		id, err := res.LastInsertId()
		if err != nil {
			return err
		}
		w.currentSpanID = id
	} else {
		w.currentSpanID = -1
		if err := w.db.QueryRow(
			`SELECT id FROM area_time_spans WHERE session_id=? AND entered_at=?`, w.sessionID, ts,
		).Scan(&w.currentSpanID); err != nil && err != sql.ErrNoRows {
			return err
		}
	}
	w.currentSpanAfkSecs = 0
	return nil
}

func (w *Writer) closeSession(endTs string) error {
	if w.sessionID < 0 || endTs == "" {
		return nil
	}
	if err := w.closeSpan(endTs); err != nil {
		return err
	}

	if w.altTabOutTs != "" {
		if _, err := w.db.Exec(
			`UPDATE session_alt_tabs SET in_at=? WHERE session_id=? AND out_at=?`,
			endTs, w.sessionID, w.altTabOutTs); err != nil {
			return err
		}
		w.altTabOutTs = ""
	}

	if w.afkOnTs != "" {
		w.sessionAfkSecs += max0(tsToSecs(endTs) - tsToSecs(w.afkOnTs))
		if _, err := w.db.Exec(
			`INSERT INTO session_afk(session_id, afk_on_at, afk_off_at) VALUES(?,?,?)
			 ON CONFLICT(session_id, afk_on_at) DO UPDATE SET afk_off_at=excluded.afk_off_at`,
			w.sessionID, w.afkOnTs, endTs); err != nil {
			return err
		}
		w.afkOnTs = ""
	}

	totalSecs := max0(tsToSecs(endTs) - tsToSecs(w.sessionStartTs))
	activeSecs := max0(totalSecs - w.sessionAfkSecs)

	_, err := w.db.Exec(
		`UPDATE sessions SET ended_at=?, total_secs=?, afk_secs=?, active_secs=?, char_id=?, area_id=? WHERE id=?`,
		endTs, totalSecs, w.sessionAfkSecs, activeSecs, nullIfNeg(w.sessionCharID), nullIfNeg(w.sessionAreaID), w.sessionID)

	w.sessionID = -1
	w.sessionStartTs = ""
	w.sessionAfkSecs = 0
	w.sessionCharID = -1
	w.sessionAreaID = -1
	w.lastPvpQueueEventID = -1
	return err
}

// ── event handlers ────────────────────────────────────────────────────────

func (w *Writer) handleAreaEntered(evt proto.ParsedEvent) error {
	code := str(evt.Data, "area_code")
	name := str(evt.Data, "area_name")
	level := intVal(evt.Data, "area_level")
	ts := evt.Timestamp

	if _, err := w.db.Exec(
		`INSERT INTO areas(code, level, display_name) VALUES(?,?,?)
		 ON CONFLICT(code) DO UPDATE SET level=excluded.level, display_name=excluded.display_name`,
		code, level, name); err != nil {
		return err
	}

	var areaID int64 = -1
	var areaType, areaSubtype sql.NullString
	err := w.db.QueryRow(`SELECT id, type, subtype FROM areas WHERE code=?`, code).Scan(&areaID, &areaType, &areaSubtype)
	if err != nil && err != sql.ErrNoRows {
		return err
	}
	if areaID < 0 {
		return nil
	}

	if err := w.ensureSession(ts); err != nil {
		return err
	}
	if _, err := w.db.Exec(
		`INSERT OR IGNORE INTO area_moves(install_id, area_id, entered_at) VALUES(?,?,?)`,
		w.installID, areaID, ts); err != nil {
		return err
	}
	w.sessionAreaID = areaID

	if err := w.closeSpan(ts); err != nil {
		return err
	}
	if err := w.openSpan(ts, areaID); err != nil {
		return err
	}

	// Enrich the event in place so the live "clientlog" broadcast (if this
	// event type is allow-listed) carries the same area type/subtype the old
	// C++ live event did.
	evt.Data["area_type"] = areaType.String
	evt.Data["area_subtype"] = areaSubtype.String
	return nil
}

func (w *Writer) handleLevelUp(evt proto.ParsedEvent) error {
	charName := str(evt.Data, "character")
	charClass := str(evt.Data, "char_class")
	level := intVal(evt.Data, "level")
	ts := evt.Timestamp

	if _, err := w.db.Exec(`INSERT OR IGNORE INTO classes(name) VALUES(?)`, charClass); err != nil {
		return err
	}
	var classID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM classes WHERE name=?`, charClass).Scan(&classID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if classID < 0 {
		return nil
	}

	if _, err := w.db.Exec(
		`INSERT INTO characters(name, class_id, level) VALUES(?,?,?)
		 ON CONFLICT(name) DO UPDATE SET class_id=excluded.class_id, level=excluded.level`,
		charName, classID, level); err != nil {
		return err
	}
	var charID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM characters WHERE name=?`, charName).Scan(&charID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if charID < 0 {
		return nil
	}

	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO character_level_events(install_id, char_id, level, occurred_at) VALUES(?,?,?,?)`,
		w.installID, charID, level, ts)
	if err != nil {
		return err
	}
	if err := w.insertEvent(res, ts, "level_up"); err != nil {
		return err
	}

	w.sessionCharID = charID
	w.sessionCharLevel = level

	if w.currentSpanID >= 0 {
		if _, err := w.db.Exec(`UPDATE area_time_spans SET char_id=? WHERE id=?`, charID, w.currentSpanID); err != nil {
			return err
		}
		if _, err := w.db.Exec(
			`UPDATE characters SET played_secs=MAX(played_secs,
			   COALESCE((SELECT MAX(played_secs) FROM character_played_events WHERE span_id=?),0)) WHERE id=?`,
			w.currentSpanID, charID); err != nil {
			return err
		}
	}
	return nil
}

func (w *Writer) handleAfkOff(ts string) error {
	if w.afkOnTs == "" {
		return nil
	}
	dur := max0(tsToSecs(ts) - tsToSecs(w.afkOnTs))
	w.sessionAfkSecs += dur
	w.currentSpanAfkSecs += dur

	if w.sessionID >= 0 {
		if _, err := w.db.Exec(
			`INSERT INTO session_afk(session_id, afk_on_at, afk_off_at) VALUES(?,?,?)
			 ON CONFLICT(session_id, afk_on_at) DO UPDATE SET afk_off_at=excluded.afk_off_at`,
			w.sessionID, w.afkOnTs, ts); err != nil {
			return err
		}
	}
	w.afkOnTs = ""
	return nil
}

func (w *Writer) handleAltTabOut(ts string) error {
	w.altTabOutTs = ts
	if w.sessionID < 0 {
		return nil
	}
	_, err := w.db.Exec(`INSERT OR IGNORE INTO session_alt_tabs(session_id, out_at) VALUES(?,?)`, w.sessionID, ts)
	return err
}

func (w *Writer) handleAltTabBack(ts string) error {
	if w.altTabOutTs == "" {
		return nil
	}
	if w.sessionID >= 0 {
		if _, err := w.db.Exec(
			`UPDATE session_alt_tabs SET in_at=? WHERE session_id=? AND out_at=?`,
			ts, w.sessionID, w.altTabOutTs); err != nil {
			return err
		}
	}
	w.altTabOutTs = ""
	return nil
}

func (w *Writer) handleQuestEvent(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	eventType := str(evt.Data, "event_type")
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO quest_events(session_id, area_id, event_type, occurred_at) VALUES(?,?,?,?)`,
		w.sessionID, nullIfNeg(w.sessionAreaID), eventType, evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "quest")
}

func (w *Writer) handleGeneralEvent(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	eventType := str(evt.Data, "event_type")
	if eventType == "ruleset_failed" {
		res, err := w.db.Exec(
			`INSERT OR IGNORE INTO zone_ruleset_failed_events(session_id, area_id, ruleset_name, occurred_at) VALUES(?,?,?,?)`,
			w.sessionID, nullIfNeg(w.sessionAreaID), str(evt.Data, "ruleset"), evt.Timestamp)
		if err != nil {
			return err
		}
		return w.insertEvent(res, evt.Timestamp, "ruleset_failed")
	}
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO general_events(session_id, area_id, event_type, occurred_at) VALUES(?,?,?,?)`,
		w.sessionID, nullIfNeg(w.sessionAreaID), eventType, evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "general")
}

func (w *Writer) handlePlayed(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	playedSecs := int64Val(evt.Data, "played_secs")
	if _, err := w.db.Exec(
		`INSERT OR IGNORE INTO character_played_events(session_id, span_id, played_secs, occurred_at) VALUES(?,?,?,?)`,
		w.sessionID, nullIfNeg(w.currentSpanID), playedSecs, evt.Timestamp); err != nil {
		return err
	}
	if w.sessionCharID >= 0 {
		if _, err := w.db.Exec(
			`UPDATE characters SET played_secs=MAX(played_secs,?) WHERE id=?`, playedSecs, w.sessionCharID); err != nil {
			return err
		}
	}
	return nil
}

func (w *Writer) handleAchievement(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	code := str(evt.Data, "name")
	if _, err := w.db.Exec(`INSERT OR IGNORE INTO achievements(code) VALUES(?)`, code); err != nil {
		return err
	}
	var achievID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM achievements WHERE code=?`, code).Scan(&achievID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if achievID < 0 {
		return nil
	}
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO achievement_events(session_id, achievement_id, occurred_at) VALUES(?,?,?)`,
		w.sessionID, achievID, evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "achievement")
}

func (w *Writer) handleHideoutDiscovered(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	name := str(evt.Data, "name")
	if _, err := w.db.Exec(`INSERT OR IGNORE INTO hideouts(name) VALUES(?)`, name); err != nil {
		return err
	}
	var hideoutID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM hideouts WHERE name=?`, name).Scan(&hideoutID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if hideoutID < 0 {
		return nil
	}
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO hideout_discovered_events(session_id, hideout_id, area_id, occurred_at) VALUES(?,?,?,?)`,
		w.sessionID, hideoutID, nullIfNeg(w.sessionAreaID), evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "hideout")
}

func (w *Writer) handlePvpQueue(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	name := str(evt.Data, "match_name")
	playerCount := intVal(evt.Data, "other_players")

	if _, err := w.db.Exec(`INSERT OR IGNORE INTO pvp_matches(name) VALUES(?)`, name); err != nil {
		return err
	}
	var matchID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM pvp_matches WHERE name=?`, name).Scan(&matchID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if matchID < 0 {
		return nil
	}

	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO pvp_queue_events(session_id, match_id, player_count, occurred_at) VALUES(?,?,?,?)`,
		w.sessionID, matchID, playerCount, evt.Timestamp)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n > 0 {
		id, err := res.LastInsertId()
		if err != nil {
			return err
		}
		w.lastPvpQueueEventID = id
	}
	return w.insertEvent(res, evt.Timestamp, "pvp_queue")
}

func (w *Writer) handlePvpQueueCancelled(ts string) error {
	if w.lastPvpQueueEventID < 0 {
		return nil
	}
	_, err := w.db.Exec(`UPDATE pvp_queue_events SET cancelled_at=? WHERE id=?`, ts, w.lastPvpQueueEventID)
	w.lastPvpQueueEventID = -1
	return err
}

func (w *Writer) handlePassiveAlloc(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	code := str(evt.Data, "skill_id")
	name := str(evt.Data, "skill_name")
	isMastery := 0
	if boolVal(evt.Data, "is_mastery") {
		isMastery = 1
	}
	action := "allocated"
	if evt.Type == proto.EventPassiveUnallocated {
		action = "unallocated"
	}

	if _, err := w.db.Exec(
		`INSERT INTO passive_skills(code, name, is_mastery) VALUES(?,?,?)
		 ON CONFLICT(code) DO UPDATE SET name=excluded.name`,
		code, name, isMastery); err != nil {
		return err
	}
	var passiveID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM passive_skills WHERE code=?`, code).Scan(&passiveID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if passiveID < 0 {
		return nil
	}

	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO passive_skill_allocations(session_id, char_id, passive_skill_id, action, allocated_at) VALUES(?,?,?,?,?)`,
		w.sessionID, nullIfNeg(w.sessionCharID), passiveID, action, evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "passive_alloc")
}

// resolveGuildID looks up (and creates if necessary) the guilds row for tag,
// returning -1 if tag is empty.
func (w *Writer) resolveGuildID(tag string) (int64, error) {
	if tag == "" {
		return -1, nil
	}
	if _, err := w.db.Exec(`INSERT OR IGNORE INTO guilds(tag) VALUES(?)`, tag); err != nil {
		return -1, err
	}
	var guildID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM guilds WHERE tag=?`, tag).Scan(&guildID); err != nil && err != sql.ErrNoRows {
		return -1, err
	}
	return guildID, nil
}

func (w *Writer) handleWhisper(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	guildID, err := w.resolveGuildID(str(evt.Data, "guild_tag"))
	if err != nil {
		return err
	}
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO whispers(session_id, direction, player_name, guild_id, message, occurred_at) VALUES(?,?,?,?,?,?)`,
		w.sessionID, str(evt.Data, "direction"), str(evt.Data, "player"), nullIfNeg(guildID), str(evt.Data, "message"), evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "whisper")
}

func (w *Writer) handleChat(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	guildID, err := w.resolveGuildID(str(evt.Data, "guild_tag"))
	if err != nil {
		return err
	}
	speaker := str(evt.Data, "player")
	if _, err := w.db.Exec(`INSERT OR IGNORE INTO public_chars(name) VALUES(?)`, speaker); err != nil {
		return err
	}
	var pubCharID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM public_chars WHERE name=?`, speaker).Scan(&pubCharID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if pubCharID < 0 {
		return nil
	}
	if guildID >= 0 {
		if _, err := w.db.Exec(`UPDATE public_chars SET guild_id=? WHERE id=?`, guildID, pubCharID); err != nil {
			return err
		}
	}
	_, err = w.db.Exec(
		`INSERT OR IGNORE INTO chats(session_id, public_char_id, channel, guild_id, message, occurred_at) VALUES(?,?,?,?,?,?)`,
		w.sessionID, pubCharID, str(evt.Data, "channel"), nullIfNeg(guildID), str(evt.Data, "message"), evt.Timestamp)
	return err
}

func (w *Writer) handleCharacterDeath(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	name := str(evt.Data, "character")
	var deadCharID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM characters WHERE name=?`, name).Scan(&deadCharID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if deadCharID < 0 {
		return nil
	}
	var level any
	if deadCharID == w.sessionCharID && w.sessionCharLevel > 0 {
		level = w.sessionCharLevel
	}
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO character_deaths(session_id, char_id, area_id, level, occurred_at) VALUES(?,?,?,?,?)`,
		w.sessionID, deadCharID, nullIfNeg(w.sessionAreaID), level, evt.Timestamp)
	if err != nil {
		return err
	}
	return w.insertEvent(res, evt.Timestamp, "death")
}

func (w *Writer) handleGuildJoined(evt proto.ParsedEvent) error {
	w.currentGuild = str(evt.Data, "guild_name")
	_, err := w.db.Exec(
		`INSERT INTO accounts(name, guild_name) VALUES('unknown', ?)
		 ON CONFLICT(name) DO UPDATE SET guild_name=excluded.guild_name`,
		w.currentGuild)
	return err
}

func (w *Writer) handleGuildMemberUpdated(evt proto.ParsedEvent) error {
	name := str(evt.Data, "account_name")
	if _, err := w.db.Exec(`INSERT OR IGNORE INTO accounts(name) VALUES(?)`, name); err != nil {
		return err
	}
	if w.currentGuild == "" {
		return nil
	}
	_, err := w.db.Exec(
		`INSERT OR IGNORE INTO guild_members(guild_name, account_id) SELECT ?, id FROM accounts WHERE name=?`,
		w.currentGuild, name)
	return err
}

func (w *Writer) handleChatChannelJoin(evt proto.ParsedEvent) error {
	num := intVal(evt.Data, "number")
	lang := str(evt.Data, "lang")

	// Labels are registered independently via the channels.register WS method
	// (internal/channels), not baked in at ingest time, so this only tracks
	// which channel numbers exist and their language.
	_, err := w.db.Exec(
		`INSERT INTO chat_channels(number, lang) VALUES(?,?)
		 ON CONFLICT(number) DO UPDATE SET lang=excluded.lang`,
		num, lang)
	if err != nil {
		return err
	}
	var channelID int64 = -1
	if err := w.db.QueryRow(`SELECT id FROM chat_channels WHERE number=?`, num).Scan(&channelID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if channelID < 0 {
		return nil
	}
	_, err = w.db.Exec(
		`INSERT OR IGNORE INTO chat_channel_joins(install_id, channel_id, joined_at) VALUES(?,?,?)`,
		w.installID, channelID, evt.Timestamp)
	return err
}

func (w *Writer) handlePassivesSnapshot(evt proto.ParsedEvent) error {
	if w.sessionID < 0 {
		return nil
	}
	ts := evt.Timestamp
	if _, err := w.db.Exec(
		`INSERT OR IGNORE INTO passive_point_snapshots
		   (session_id, char_id, occurred_at, total_points, allocated_points,
		    ascendancy_total, ascendancy_allocated, level_points, quest_points)
		 VALUES(?,?,?,?,?,?,?,?,?)`,
		w.sessionID, nullIfNeg(w.sessionCharID), ts,
		intVal(evt.Data, "total_points"), intVal(evt.Data, "allocated_points"),
		intVal(evt.Data, "asc_total"), intVal(evt.Data, "asc_allocated"),
		intVal(evt.Data, "level_points"), intVal(evt.Data, "quest_points")); err != nil {
		return err
	}

	var snapID int64 = -1
	if err := w.db.QueryRow(
		`SELECT id FROM passive_point_snapshots WHERE session_id=? AND occurred_at=?`, w.sessionID, ts,
	).Scan(&snapID); err != nil && err != sql.ErrNoRows {
		return err
	}
	if snapID < 0 {
		return nil
	}

	quests, _ := evt.Data["quests"].([]map[string]any)
	for _, q := range quests {
		name, _ := q["name"].(string)
		points := intVal(q, "points")

		if _, err := w.db.Exec(`INSERT OR IGNORE INTO passive_quest_sources(name) VALUES(?)`, name); err != nil {
			return err
		}
		var questID int64 = -1
		if err := w.db.QueryRow(`SELECT id FROM passive_quest_sources WHERE name=?`, name).Scan(&questID); err != nil && err != sql.ErrNoRows {
			return err
		}
		if questID < 0 {
			continue
		}
		if _, err := w.db.Exec(
			`INSERT OR IGNORE INTO passive_snapshot_quests(snapshot_id, quest_id, points) VALUES(?,?,?)`,
			snapID, questID, points); err != nil {
			return err
		}
	}
	return nil
}

func (w *Writer) handleClientScreenEvent(ts, screenType string) error {
	res, err := w.db.Exec(
		`INSERT OR IGNORE INTO client_screen_events(install_id, event_type, occurred_at) VALUES(?,?,?)`,
		w.installID, screenType, ts)
	if err != nil {
		return err
	}
	return w.insertEvent(res, ts, "client_screen")
}
