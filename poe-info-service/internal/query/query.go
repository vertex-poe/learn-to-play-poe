package query

import (
	"database/sql"
	"fmt"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/schema"
	_ "modernc.org/sqlite"
)

type ChatRecord struct {
	Source     string `json:"source"`
	Channel    string `json:"channel"`
	PlayerName string `json:"player_name"`
	GuildTag   string `json:"guild_tag"`
	Message    string `json:"message"`
	OccurredAt string `json:"occurred_at"`
}

type WhisperRecord struct {
	Direction  string `json:"direction"`
	PlayerName string `json:"player_name"`
	GuildTag   string `json:"guild_tag"`
	Message    string `json:"message"`
	OccurredAt string `json:"occurred_at"`
}

type DB struct {
	db *sql.DB
}

// New ensures the game-history schema (schema.sql) is created/migrated on
// db and returns a DB wrapping it. poe-info-service is the sole writer of
// this database, so this is also where schema ownership lives. db is shared
// with the store package on the same physical file (ADR-006) — lifecycle
// (opening and closing the connection) belongs to whoever passed it in.
func New(db *sql.DB) (*DB, error) {
	if err := schema.EnsureSchema(db); err != nil {
		return nil, fmt.Errorf("ensure schema: %w", err)
	}
	return &DB{db: db}, nil
}

// Raw exposes the underlying *sql.DB so the ingest package can share this
// connection for writes rather than opening a second one to the same file.
func (d *DB) Raw() *sql.DB { return d.db }

// FetchChats mirrors Database::fetchChats from the C++ side. Results are
// returned in chronological order (oldest first).
func (d *DB) FetchChats(channels []string, includeDms bool, limit, offset int, fromDate, toDate string) ([]ChatRecord, error) {
	var parts []string
	var args []any

	if len(channels) > 0 {
		ph := strings.Repeat("?,", len(channels))
		ph = ph[:len(ph)-1]
		part := fmt.Sprintf(
			"SELECT 'chat' AS source, c.channel, pc.name AS player_name,"+
				" COALESCE(g.tag,'') AS guild_tag, c.message, c.occurred_at"+
				" FROM chats c"+
				" JOIN public_chars pc ON pc.id=c.public_char_id"+
				" LEFT JOIN guilds g ON g.id=c.guild_id"+
				" WHERE c.channel IN (%s)", ph)
		for _, ch := range channels {
			args = append(args, ch)
		}
		if fromDate != "" {
			part += " AND c.occurred_at >= ?"
			args = append(args, fromDate+" 00:00:00")
		}
		if toDate != "" {
			part += " AND c.occurred_at < ?"
			args = append(args, nextDay(toDate))
		}
		parts = append(parts, part)
	}

	if includeDms {
		part := "SELECT 'dm' AS source," +
			" CASE direction WHEN 'from' THEN '@from' ELSE '@to' END AS channel," +
			" w.player_name, COALESCE(g.tag,'') AS guild_tag, w.message, w.occurred_at" +
			" FROM whispers w LEFT JOIN guilds g ON g.id=w.guild_id"
		var conds []string
		if fromDate != "" {
			conds = append(conds, "w.occurred_at >= ?")
			args = append(args, fromDate+" 00:00:00")
		}
		if toDate != "" {
			conds = append(conds, "w.occurred_at < ?")
			args = append(args, nextDay(toDate))
		}
		if len(conds) > 0 {
			part += " WHERE " + strings.Join(conds, " AND ")
		}
		parts = append(parts, part)
	}

	if len(parts) == 0 {
		return nil, nil
	}

	q := "SELECT source,channel,player_name,guild_tag,message,occurred_at FROM (" +
		strings.Join(parts, " UNION ALL ") + ") ORDER BY occurred_at DESC"
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d OFFSET %d", limit, offset)
	}

	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchChats: %w", err)
	}
	defer rows.Close()

	var out []ChatRecord
	for rows.Next() {
		var r ChatRecord
		if err := rows.Scan(&r.Source, &r.Channel, &r.PlayerName, &r.GuildTag, &r.Message, &r.OccurredAt); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	reverseSlice(out)
	return out, nil
}

// FetchWhispers mirrors Database::fetchWhispers. Results are returned in
// chronological order (oldest first).
func (d *DB) FetchWhispers(playerFilter string, limit, offset int) ([]WhisperRecord, error) {
	q := "SELECT w.direction, w.player_name, COALESCE(g.tag,'') AS guild_tag," +
		" w.message, w.occurred_at" +
		" FROM whispers w LEFT JOIN guilds g ON g.id=w.guild_id"
	var args []any
	if playerFilter != "" {
		q += " WHERE w.player_name = ?"
		args = append(args, playerFilter)
	}
	q += " ORDER BY w.occurred_at DESC"
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d OFFSET %d", limit, offset)
	}

	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchWhispers: %w", err)
	}
	defer rows.Close()

	var out []WhisperRecord
	for rows.Next() {
		var r WhisperRecord
		if err := rows.Scan(&r.Direction, &r.PlayerName, &r.GuildTag, &r.Message, &r.OccurredAt); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	reverseSlice(out)
	return out, nil
}

// ── Sessions ─────────────────────────────────────────────────────────────────

type SessionRecord struct {
	ID          int64  `json:"id"`
	StartedAt   string `json:"started_at"`
	EndedAt     string `json:"ended_at"`
	TotalSecs   int    `json:"total_secs"`
	ActiveSecs  int    `json:"active_secs"`
	AccountName string `json:"account_name"`
	CharName    string `json:"char_name"`
	CharClass   string `json:"char_class"`
	InstallPath string `json:"install_path"`
}

type ZoneTransitionRecord struct {
	AreaName     string `json:"area_name"`
	AreaCode     string `json:"area_code"`
	AreaType     string `json:"area_type"`
	AreaSubtype  string `json:"area_subtype"`
	AreaLevel    int    `json:"area_level"`
	EnteredAt    string `json:"entered_at"`
	DurationSecs int    `json:"duration_secs"`
}

type SessionEventRecord struct {
	EventType   string `json:"event_type"`
	OccurredAt  string `json:"occurred_at"`
	CharName    string `json:"char_name"`
	CharClass   string `json:"char_class"`
	InstallPath string `json:"install_path"`
	ActiveSecs  int    `json:"active_secs"`
	TotalSecs   int    `json:"total_secs"`
}

type ClientScreenEventRecord struct {
	EventType  string `json:"event_type"`
	OccurredAt string `json:"occurred_at"`
}

type AfkRecord struct {
	AfkOnAt      string `json:"afk_on_at"`
	AfkOffAt     string `json:"afk_off_at"`
	DurationSecs int    `json:"duration_secs"`
}

type AltTabRecord struct {
	OutAt        string `json:"out_at"`
	InAt         string `json:"in_at"`
	DurationSecs int    `json:"duration_secs"`
}

type SessionPageData struct {
	Zones              []ZoneTransitionRecord    `json:"zones"`
	SessionEvents      []SessionEventRecord      `json:"session_events"`
	ClientScreenEvents []ClientScreenEventRecord `json:"client_screen_events"`
	AfkRecords         []AfkRecord               `json:"afk_records"`
	AltTabRecords      []AltTabRecord            `json:"alt_tab_records"`
}

// FetchSessions mirrors Database::fetchSessions. Results are returned in
// chronological order (oldest first).
func (d *DB) FetchSessions(limit, offset int) ([]SessionRecord, error) {
	q := `SELECT s.id, s.started_at, COALESCE(s.ended_at,''),
	             COALESCE(s.total_secs,-1), COALESCE(s.active_secs,-1),
	             COALESCE(a.name,''), COALESCE(c.name,''), COALESCE(cl.name,''),
	             i.path
	      FROM sessions s
	      JOIN installs i        ON s.install_id = i.id
	      LEFT JOIN accounts a   ON s.account_id = a.id
	      LEFT JOIN characters c ON s.char_id    = c.id
	      LEFT JOIN classes cl   ON c.class_id   = cl.id
	      ORDER BY s.started_at DESC`
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d OFFSET %d", limit, offset)
	} else if offset > 0 {
		q += fmt.Sprintf(" LIMIT -1 OFFSET %d", offset)
	}
	rows, err := d.db.Query(q)
	if err != nil {
		return nil, fmt.Errorf("fetchSessions: %w", err)
	}
	defer rows.Close()
	var out []SessionRecord
	for rows.Next() {
		var r SessionRecord
		if err := rows.Scan(&r.ID, &r.StartedAt, &r.EndedAt, &r.TotalSecs, &r.ActiveSecs,
			&r.AccountName, &r.CharName, &r.CharClass, &r.InstallPath); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	reverseSlice(out)
	return out, nil
}

// FetchZoneTransitions returns zone transitions for a session, newest first
// (DESC). sessionID=-1 targets the most recent open session.
func (d *DB) FetchZoneTransitions(sessionID int64, limit, offset int) ([]ZoneTransitionRecord, error) {
	var q string
	var args []any
	if sessionID < 0 {
		q = `SELECT COALESCE(a.display_name, a.code), a.code,
		            COALESCE(a.type,''), COALESCE(a.subtype,''), COALESCE(a.level,0),
		            ats.entered_at, COALESCE(ats.duration_secs,-1)
		     FROM area_time_spans ats
		     LEFT JOIN areas a ON ats.area_id = a.id
		     WHERE ats.session_id = (
		         SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1
		     ) AND ats.area_id IS NOT NULL
		     ORDER BY ats.entered_at DESC`
	} else {
		q = `SELECT COALESCE(a.display_name, a.code), a.code,
		            COALESCE(a.type,''), COALESCE(a.subtype,''), COALESCE(a.level,0),
		            ats.entered_at, COALESCE(ats.duration_secs,-1)
		     FROM area_time_spans ats
		     LEFT JOIN areas a ON ats.area_id = a.id
		     WHERE ats.session_id = ? AND ats.area_id IS NOT NULL
		     ORDER BY ats.entered_at DESC`
		args = append(args, sessionID)
	}
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d OFFSET %d", limit, offset)
	}
	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchZoneTransitions: %w", err)
	}
	defer rows.Close()
	var out []ZoneTransitionRecord
	for rows.Next() {
		var r ZoneTransitionRecord
		if err := rows.Scan(&r.AreaName, &r.AreaCode, &r.AreaType, &r.AreaSubtype,
			&r.AreaLevel, &r.EnteredAt, &r.DurationSecs); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

// FetchSessionEvents returns game-start/stop events in chronological order
// (oldest first).
func (d *DB) FetchSessionEvents(limit int) ([]SessionEventRecord, error) {
	q := `SELECT event_type, occurred_at, char_name, char_class, install_path, active_secs, total_secs
	      FROM (
	          SELECT 'start' AS event_type, s.started_at AS occurred_at,
	                 COALESCE(c.name,'') AS char_name, COALESCE(cl.name,'') AS char_class,
	                 i.path AS install_path, COALESCE(s.active_secs,-1), COALESCE(s.total_secs,-1)
	          FROM sessions s
	          JOIN installs i        ON s.install_id = i.id
	          LEFT JOIN characters c ON s.char_id    = c.id
	          LEFT JOIN classes cl   ON c.class_id   = cl.id
	          UNION ALL
	          SELECT 'stop', s.ended_at, COALESCE(c.name,''), COALESCE(cl.name,''), i.path,
	                 COALESCE(s.active_secs,-1), COALESCE(s.total_secs,-1)
	          FROM sessions s
	          JOIN installs i        ON s.install_id = i.id
	          LEFT JOIN characters c ON s.char_id    = c.id
	          LEFT JOIN classes cl   ON c.class_id   = cl.id
	          WHERE s.ended_at IS NOT NULL
	      )
	      ORDER BY occurred_at DESC`
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d", limit)
	}
	rows, err := d.db.Query(q)
	if err != nil {
		return nil, fmt.Errorf("fetchSessionEvents: %w", err)
	}
	defer rows.Close()
	var out []SessionEventRecord
	for rows.Next() {
		var r SessionEventRecord
		if err := rows.Scan(&r.EventType, &r.OccurredAt, &r.CharName, &r.CharClass,
			&r.InstallPath, &r.ActiveSecs, &r.TotalSecs); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, err
	}
	reverseSlice(out)
	return out, nil
}

// FetchClientScreenEvents returns client-screen events for a session, newest
// first. sessionID=-1 targets the most recent open session.
func (d *DB) FetchClientScreenEvents(sessionID int64) ([]ClientScreenEventRecord, error) {
	var q string
	var args []any
	if sessionID < 0 {
		q = `SELECT cse.event_type, cse.occurred_at
		     FROM client_screen_events cse
		     JOIN sessions s ON cse.install_id = s.install_id
		     WHERE s.ended_at IS NULL
		       AND s.id = (SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1)
		       AND cse.occurred_at >= s.started_at
		     ORDER BY cse.occurred_at DESC`
	} else {
		q = `SELECT cse.event_type, cse.occurred_at
		     FROM client_screen_events cse
		     JOIN sessions s ON cse.install_id = s.install_id
		     WHERE s.id = ?
		       AND cse.occurred_at >= s.started_at
		       AND (s.ended_at IS NULL OR cse.occurred_at <= s.ended_at)
		     ORDER BY cse.occurred_at DESC`
		args = append(args, sessionID)
	}
	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchClientScreenEvents: %w", err)
	}
	defer rows.Close()
	var out []ClientScreenEventRecord
	for rows.Next() {
		var r ClientScreenEventRecord
		if err := rows.Scan(&r.EventType, &r.OccurredAt); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

// FetchAfkRecords returns AFK intervals for a session, newest first.
// sessionID=-1 targets the most recent open session.
func (d *DB) FetchAfkRecords(sessionID int64, limit int) ([]AfkRecord, error) {
	var q string
	var args []any
	if sessionID < 0 {
		q = `SELECT afk_on_at, COALESCE(afk_off_at,''),
		            COALESCE(CAST((strftime('%s',afk_off_at)-strftime('%s',afk_on_at)) AS INTEGER),-1)
		     FROM session_afk
		     WHERE session_id = (SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1)
		     ORDER BY afk_on_at DESC`
	} else {
		q = `SELECT afk_on_at, COALESCE(afk_off_at,''),
		            COALESCE(CAST((strftime('%s',afk_off_at)-strftime('%s',afk_on_at)) AS INTEGER),-1)
		     FROM session_afk
		     WHERE session_id = ?
		     ORDER BY afk_on_at DESC`
		args = append(args, sessionID)
	}
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d", limit)
	}
	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchAfkRecords: %w", err)
	}
	defer rows.Close()
	var out []AfkRecord
	for rows.Next() {
		var r AfkRecord
		if err := rows.Scan(&r.AfkOnAt, &r.AfkOffAt, &r.DurationSecs); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

// FetchAltTabRecords returns alt-tab intervals for a session, newest first.
// sessionID=-1 targets the most recent open session.
func (d *DB) FetchAltTabRecords(sessionID int64, limit int) ([]AltTabRecord, error) {
	var q string
	var args []any
	if sessionID < 0 {
		q = `SELECT out_at, COALESCE(in_at,''),
		            COALESCE(CAST((strftime('%s',in_at)-strftime('%s',out_at)) AS INTEGER),-1)
		     FROM session_alt_tabs
		     WHERE session_id = (SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1)
		     ORDER BY out_at DESC`
	} else {
		q = `SELECT out_at, COALESCE(in_at,''),
		            COALESCE(CAST((strftime('%s',in_at)-strftime('%s',out_at)) AS INTEGER),-1)
		     FROM session_alt_tabs
		     WHERE session_id = ?
		     ORDER BY out_at DESC`
		args = append(args, sessionID)
	}
	if limit > 0 {
		q += fmt.Sprintf(" LIMIT %d", limit)
	}
	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchAltTabRecords: %w", err)
	}
	defer rows.Close()
	var out []AltTabRecord
	for rows.Next() {
		var r AltTabRecord
		if err := rows.Scan(&r.OutAt, &r.InAt, &r.DurationSecs); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

// FetchSessionPageData bundles all detail data for one session in a single
// call. sessionID=-1 uses the most recent open session.
func (d *DB) FetchSessionPageData(sessionID int64, sessionEventLimit, zoneLimit int) (SessionPageData, error) {
	var data SessionPageData
	var err error

	if sessionID < 0 && sessionEventLimit > 0 {
		data.SessionEvents, err = d.FetchSessionEvents(sessionEventLimit)
		if err != nil {
			return data, err
		}
	}

	if zoneLimit > 0 {
		data.Zones, err = d.FetchZoneTransitions(sessionID, zoneLimit, 0)
		if err != nil {
			return data, err
		}
		data.ClientScreenEvents, err = d.FetchClientScreenEvents(sessionID)
		if err != nil {
			return data, err
		}
		data.AfkRecords, err = d.FetchAfkRecords(sessionID, zoneLimit)
		if err != nil {
			return data, err
		}
		data.AltTabRecords, err = d.FetchAltTabRecords(sessionID, zoneLimit)
		if err != nil {
			return data, err
		}
	}

	return data, nil
}

// FetchChatDates returns distinct dates ("YYYY-MM-DD") that have data for the
// given channel/DM filter, most-recent first. Mirrors Database::fetchChatDates.
func (d *DB) FetchChatDates(channels []string, includeDms bool) ([]string, error) {
	if len(channels) == 0 && !includeDms {
		return nil, nil
	}
	var parts []string
	var args []any
	if len(channels) > 0 {
		ph := strings.Repeat("?,", len(channels))
		ph = ph[:len(ph)-1]
		parts = append(parts, fmt.Sprintf(
			"SELECT occurred_at FROM chats WHERE channel IN (%s)", ph))
		for _, ch := range channels {
			args = append(args, ch)
		}
	}
	if includeDms {
		parts = append(parts, "SELECT occurred_at FROM whispers")
	}
	q := "SELECT DISTINCT date(occurred_at) FROM (" +
		strings.Join(parts, " UNION ALL ") +
		") ORDER BY 1 DESC"
	rows, err := d.db.Query(q, args...)
	if err != nil {
		return nil, fmt.Errorf("fetchChatDates: %w", err)
	}
	defer rows.Close()
	var out []string
	for rows.Next() {
		var date string
		if err := rows.Scan(&date); err != nil {
			return nil, err
		}
		out = append(out, date)
	}
	return out, rows.Err()
}

// PartnerRecord mirrors Database::PartnerRecord from the C++ side.
type PartnerRecord struct {
	Name  string   `json:"name"`
	Dates []string `json:"dates"`
}

// FetchWhisperPartnersWithDates mirrors Database::fetchWhisperPartnersWithDates.
// Returns partners ordered by most-recent activity, each with their distinct
// active dates (YYYY-MM-DD), most-recent first.
func (d *DB) FetchWhisperPartnersWithDates() ([]PartnerRecord, error) {
	// Pass 1: partners in most-recently-active order.
	orderRows, err := d.db.Query(
		"SELECT player_name FROM whispers GROUP BY player_name ORDER BY MAX(occurred_at) DESC")
	if err != nil {
		return nil, fmt.Errorf("fetchWhisperPartnersWithDates (pass1): %w", err)
	}
	var ordered []string
	for orderRows.Next() {
		var name string
		if err := orderRows.Scan(&name); err != nil {
			orderRows.Close()
			return nil, err
		}
		ordered = append(ordered, name)
	}
	orderRows.Close()
	if err := orderRows.Err(); err != nil {
		return nil, err
	}
	if len(ordered) == 0 {
		return nil, nil
	}

	// Pass 2: all distinct (player, date) pairs, most-recent date first per player.
	dateRows, err := d.db.Query(
		"SELECT DISTINCT player_name, date(occurred_at) FROM whispers" +
			" ORDER BY player_name ASC, date(occurred_at) DESC")
	if err != nil {
		return nil, fmt.Errorf("fetchWhisperPartnersWithDates (pass2): %w", err)
	}
	dateMap := make(map[string][]string, len(ordered))
	for dateRows.Next() {
		var nm, dt string
		if err := dateRows.Scan(&nm, &dt); err != nil {
			dateRows.Close()
			return nil, err
		}
		dateMap[nm] = append(dateMap[nm], dt)
	}
	dateRows.Close()
	if err := dateRows.Err(); err != nil {
		return nil, err
	}

	result := make([]PartnerRecord, 0, len(ordered))
	for _, nm := range ordered {
		result = append(result, PartnerRecord{Name: nm, Dates: dateMap[nm]})
	}
	return result, nil
}

func nextDay(dateStr string) string {
	t, err := time.Parse("2006-01-02", dateStr)
	if err != nil {
		return dateStr + " 00:00:00"
	}
	return t.AddDate(0, 0, 1).Format("2006-01-02") + " 00:00:00"
}

func reverseSlice[T any](s []T) {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
}
