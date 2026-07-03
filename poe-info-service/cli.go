package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/dialog"
	"github.com/MovingCairn/poe-info-service/internal/schema"
	_ "modernc.org/sqlite"
)

// cliDispatch handles subcommands that don't start the long-running service.
// It returns an exit code and true if args named a recognised subcommand;
// false means "fall through to normal service startup", mirroring l2p-poe's
// own cliDispatch (src/core/Cli.cpp).
func cliDispatch(args []string) (code int, handled bool) {
	if len(args) < 1 || args[0] != "dialog" {
		return 0, false
	}
	if len(args) < 2 || args[1] != "ingest" {
		fmt.Fprintln(os.Stderr, "usage: poe-info-service dialog ingest [file.json]")
		return 1, true
	}
	return runDialogIngest(args[2:]), true
}

// runDialogIngest persists NPC dialog entries already hashed by
// `l2p-poe dialog hash` — poe-info-service owns the database (ADR-006), so
// this is where writing to npc_dialog_entries belongs, not the C++ side.
// Input is the JSON array `dialog hash` prints: npc_name, npc_name_hash,
// message_hash. Hashing itself stays on the C++ side (src/util/DialogHash.h)
// so there's exactly one implementation of the hash algorithm.
func runDialogIngest(args []string) int {
	var (
		data []byte
		err  error
	)
	if len(args) >= 1 {
		data, err = os.ReadFile(args[0])
	} else {
		data, err = io.ReadAll(os.Stdin)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		return 1
	}

	var entries []dialog.Entry
	if err := json.Unmarshal(data, &entries); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		return 1
	}

	exe, _ := os.Executable()
	dbPath := filepath.Join(config.ResolveDir(filepath.Dir(exe)), config.DBFileName)
	db, err := sql.Open("sqlite", dbPath+"?_journal_mode=WAL&_busy_timeout=5000")
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: open %s: %v\n", dbPath, err)
		return 1
	}
	defer db.Close()
	if err := schema.EnsureSchema(db); err != nil {
		fmt.Fprintf(os.Stderr, "error: ensure schema: %v\n", err)
		return 1
	}

	inserted, err := dialog.UpsertEntries(db, entries)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		return 1
	}
	fmt.Printf("%d inserted, %d already present\n", inserted, len(entries)-inserted)
	return 0
}
