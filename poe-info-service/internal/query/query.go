package query

import (
	"database/sql"
	"fmt"
	"strings"
	"time"

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

// Open opens the l2p SQLite database read-only. WAL mode allows concurrent
// readers alongside the main l2p writer.
func Open(path string) (*DB, error) {
	dsn := path + "?mode=ro&_journal_mode=WAL&_busy_timeout=5000"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, fmt.Errorf("open %q: %w", path, err)
	}
	db.SetMaxOpenConns(1)
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, fmt.Errorf("ping %q: %w", path, err)
	}
	return &DB{db: db}, nil
}

func (d *DB) Close() error { return d.db.Close() }

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
