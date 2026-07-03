// Package channels manages user-registered chat channel labels
// (chat_channel_labels), the WebSocket-exposed replacement for l2p-poe
// handing poe-info-service a path to its own config toml to parse.
package channels

import (
	"database/sql"
	"fmt"
)

// ensureChannelID upserts chat_channels(number) and returns its id, mirroring
// the same lookup the ingest writer does for chat_channel_join events.
func ensureChannelID(db *sql.DB, number int) (int64, error) {
	if _, err := db.Exec(`INSERT OR IGNORE INTO chat_channels(number) VALUES(?)`, number); err != nil {
		return -1, err
	}
	var id int64
	if err := db.QueryRow(`SELECT id FROM chat_channels WHERE number=?`, number).Scan(&id); err != nil {
		return -1, err
	}
	return id, nil
}

// Register records label as valid for channel between validFrom and validTo
// (each "" meaning unbounded on that side). Re-registering the exact same
// (channel, label, validFrom, validTo) tuple is a no-op — dedup is the whole
// point of the unique index.
func Register(db *sql.DB, channel int, label, validFrom, validTo string) error {
	if label == "" {
		return fmt.Errorf("label required")
	}
	channelID, err := ensureChannelID(db, channel)
	if err != nil {
		return err
	}
	_, err = db.Exec(
		`INSERT OR IGNORE INTO chat_channel_labels(channel_id, label, valid_from, valid_to) VALUES(?,?,?,?)`,
		channelID, label, validFrom, validTo)
	return err
}

// Rename retargets the label text of the row identified by
// (channel, oldLabel, validFrom, validTo) to newLabel, leaving its date range
// untouched. Fails if that row doesn't exist or if newLabel already exists
// for the same channel/date range (unique index collision).
func Rename(db *sql.DB, channel int, validFrom, validTo, oldLabel, newLabel string) error {
	if newLabel == "" {
		return fmt.Errorf("new_label required")
	}
	channelID, err := ensureChannelID(db, channel)
	if err != nil {
		return err
	}
	res, err := db.Exec(
		`UPDATE chat_channel_labels SET label=? WHERE channel_id=? AND label=? AND valid_from=? AND valid_to=?`,
		newLabel, channelID, oldLabel, validFrom, validTo)
	if err != nil {
		return err
	}
	if n, _ := res.RowsAffected(); n == 0 {
		return fmt.Errorf("no matching label %q for channel %d in that date range", oldLabel, channel)
	}
	return nil
}

// Delete removes the label row identified by (channel, label, validFrom,
// validTo). Deleting a row that doesn't exist is not an error — labels are
// just cosmetic bookkeeping, so this is deliberately idempotent cleanup
// rather than a strict operation.
func Delete(db *sql.DB, channel int, label, validFrom, validTo string) error {
	channelID, err := ensureChannelID(db, channel)
	if err != nil {
		return err
	}
	_, err = db.Exec(
		`DELETE FROM chat_channel_labels WHERE channel_id=? AND label=? AND valid_from=? AND valid_to=?`,
		channelID, label, validFrom, validTo)
	return err
}
