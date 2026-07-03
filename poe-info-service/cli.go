package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/dialog"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/parser"
	"github.com/MovingCairn/poe-info-service/internal/schema"
	"github.com/MovingCairn/poe-info-service/internal/tailer"
	_ "modernc.org/sqlite"
)

// cliDispatch handles subcommands that don't start the long-running service.
// It returns an exit code and true if args named a recognised subcommand;
// false means "fall through to normal service startup", mirroring l2p-poe's
// own cliDispatch (src/core/Cli.cpp).
func cliDispatch(args []string) (code int, handled bool) {
	if len(args) < 1 {
		return 0, false
	}
	switch args[0] {
	case "dialog":
		if len(args) < 2 || args[1] != "ingest" {
			fmt.Fprintln(os.Stderr, "usage: poe-info-service dialog ingest [file.json]")
			return 1, true
		}
		return runDialogIngest(args[2:]), true

	case "log":
		if len(args) < 2 || args[1] != "ingest" {
			fmt.Fprintln(os.Stderr, "usage: poe-info-service log ingest --install-dir <dir> [--log-path <path>] [--data-dir <dir>]")
			return 1, true
		}
		return runLogIngest(args[2:]), true

	default:
		return 0, false
	}
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

// runLogIngest tails installDir's Client.txt to EOF once, applying every
// parsed event to the database via the same tailer/parser/writer pipeline
// serve() uses (internal/server/server.go), then exits. It exists for
// headless dev tooling (poe-info-service/dev/area_seeds) that wants the
// database caught up without leaving a service running — the long-running
// service otherwise only tails continuously, with no "run once and exit"
// mode. Chat channel labels are not part of this command — they're
// registered independently via the channels.register WS method
// (internal/channels), not baked in at ingest time.
func runLogIngest(args []string) int {
	fs := flag.NewFlagSet("log ingest", flag.ContinueOnError)
	installDir := fs.String("install-dir", "", "PoE install directory to ingest Client.txt from (required)")
	logPath := fs.String("log-path", "", "Path to Client.txt (default: <install-dir>/logs/Client.txt)")
	dataDir := fs.String("data-dir", "", "Directory holding poe-info-service's database (default resolves the same way poe-info-service.toml does)")
	if err := fs.Parse(args); err != nil {
		return 2
	}
	if *installDir == "" {
		fmt.Fprintln(os.Stderr, "error: --install-dir is required")
		return 1
	}

	resolvedLogPath := *logPath
	if resolvedLogPath == "" {
		resolvedLogPath = filepath.Join(*installDir, "logs", "Client.txt")
	}
	if _, err := os.Stat(resolvedLogPath); err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		return 1
	}

	resolvedDataDir := *dataDir
	if resolvedDataDir == "" {
		exe, _ := os.Executable()
		resolvedDataDir = config.ResolveDir(filepath.Dir(exe))
	}
	if err := os.MkdirAll(resolvedDataDir, 0755); err != nil {
		fmt.Fprintf(os.Stderr, "error: create %s: %v\n", resolvedDataDir, err)
		return 1
	}
	dbPath := filepath.Join(resolvedDataDir, config.DBFileName)

	db, err := sql.Open("sqlite", dbPath+"?_journal_mode=WAL&_busy_timeout=5000")
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: open %s: %v\n", dbPath, err)
		return 1
	}
	defer db.Close()
	db.SetMaxOpenConns(1) // single connection shared by the tailer and writer below
	if err := schema.EnsureSchema(db); err != nil {
		fmt.Fprintf(os.Stderr, "error: ensure schema: %v\n", err)
		return 1
	}

	installID, err := ingest.EnsureInstall(db, *installDir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: ensure install: %v\n", err)
		return 1
	}

	writer, err := ingest.NewWriter(db, installID)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: init writer: %v\n", err)
		return 1
	}

	eventCh := make(chan string, 512)
	t := tailer.New(resolvedLogPath, db, installID, eventCh)
	p := parser.New()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go t.Run(ctx)

	// Drain eventCh as lines arrive; only treat CaughtUp() as final once the
	// channel is momentarily empty, since a tick that reads new lines and the
	// tick that flips CaughtUp() true never happen in the same tailer pass
	// (see tailer.Run) — draining first before checking guarantees nothing
	// buffered is left unapplied when this loop exits.
	for {
		select {
		case line := <-eventCh:
			for _, evt := range p.ParseLine(line) {
				if err := writer.HandleEvent(evt); err != nil {
					fmt.Fprintf(os.Stderr, "warn: failed to apply %s event: %v\n", evt.Type, err)
				}
			}
			continue
		default:
		}
		if t.CaughtUp() {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}

	fmt.Printf("caught up to EOF: %s\n", resolvedLogPath)
	return 0
}
