package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/server"
)

// scanArgFlag manually looks up a flag's value from raw args, before the
// standard flag.Parse() pass runs. Needed because --data-dir/--config
// determine where the config file lives, and that file's values are used as
// defaults for other flags (port, bind, debug-logging) below — by the time
// flag.Parse() normally runs, it's too late to still be deciding where to
// load defaults from.
func scanArgFlag(args []string, name string) string {
	prefix := "--" + name
	for i, a := range args {
		if a == prefix && i+1 < len(args) {
			return args[i+1]
		}
		if v, ok := strings.CutPrefix(a, prefix+"="); ok {
			return v
		}
	}
	return ""
}

// installDirFlag collects one or more --install-dir occurrences, in order,
// via flag.Var's repeatable-flag support.
type installDirFlag []string

func (f *installDirFlag) String() string { return strings.Join(*f, ",") }
func (f *installDirFlag) Set(v string) error {
	*f = append(*f, v)
	return nil
}

// resolveInstallDirs picks every install to ingest concurrently out of the
// configured candidates — persisted poe-info-service.toml install_dirs plus
// any --install-dir flags, deduplicated in that order — each one that exists
// as a directory becomes an InstallTarget. A stale entry (a removed drive, a
// moved install) is skipped rather than handed to a tailer, which has no way
// to notice its log path will never appear and would otherwise sit in
// "ingesting" forever. Only the directory itself is checked, not Client.txt:
// a fresh, valid install nobody has launched yet legitimately has no
// Client.txt, and must not be treated as missing.
//
// explicitLogPath, when set, bypasses this search entirely — a dev
// convenience (see CONTRIBUTING.md) for pointing the service at an exact
// file regardless of what's configured; it pairs with the first candidate,
// if any, as the sole install target.
func resolveInstallDirs(persistedDirs, flagDirs []string, explicitLogPath string) []server.InstallTarget {
	dirs := make([]string, 0, len(persistedDirs)+len(flagDirs))
	seen := map[string]bool{}
	for _, dir := range append(append([]string{}, persistedDirs...), flagDirs...) {
		if dir == "" {
			continue
		}
		// Normalized before the seen check — a persisted config entry and a
		// --install-dir flag pointing at the same directory but spelled with
		// different separators would otherwise both survive this dedup and
		// reach addInstallTarget as two candidates (still deduped there, but
		// via a less obvious path — clean once, here, at the source).
		dir = ingest.NormalizeInstallPath(dir)
		if !seen[dir] {
			seen[dir] = true
			dirs = append(dirs, dir)
		}
	}

	if explicitLogPath != "" {
		var installDir string
		if len(dirs) > 0 {
			installDir = dirs[0]
		}
		return []server.InstallTarget{{Dir: installDir, LogPath: explicitLogPath}}
	}
	var targets []server.InstallTarget
	for _, dir := range dirs {
		if fi, err := os.Stat(dir); err == nil && fi.IsDir() {
			targets = append(targets, server.InstallTarget{Dir: dir, LogPath: filepath.Join(dir, "logs", "Client.txt")})
			continue
		}
		log.Printf("install dir %q not found, skipping", dir)
	}
	return targets
}

func main() {
	if code, handled := cliDispatch(os.Args[1:]); handled {
		os.Exit(code)
	}

	exe, _ := os.Executable()
	rawArgs := os.Args[1:]

	// dataDir governs the database (and the config file's default location);
	// configFile independently overrides the exact config file path. Kept
	// separate on purpose: a perf test can isolate its data dir while still
	// reading a fixed shared config, or vice versa, without either stepping
	// on the other.
	dataDir := scanArgFlag(rawArgs, "data-dir")
	if dataDir == "" {
		dataDir = config.ResolveDir(filepath.Dir(exe))
	}
	configFile := scanArgFlag(rawArgs, "config")
	if configFile == "" {
		configFile = filepath.Join(dataDir, config.FileName)
	}

	fileCfg := config.Load(configFile)
	// Materialize a default config file on first run, so the file a human
	// would want to inspect/edit during an outage always exists (ADR-006)
	// rather than only appearing after the first config.set call.
	if _, err := os.Stat(configFile); os.IsNotExist(err) {
		if err := config.Save(configFile, fileCfg); err != nil {
			log.Printf("warn: cannot write default config: %v", err)
		}
	}

	// Registered (but not captured) purely so they appear in --help output and
	// flag.Parse() doesn't reject them as unknown; their actual values were
	// already read via scanArgFlag above, before fileCfg's defaults existed.
	flag.String("data-dir", "", "Directory for poe-info-service's data (config + database); default resolves the same way poe-info-service.toml does")
	flag.String("config", "", "Exact path to poe-info-service's config file (overrides --data-dir for the config file specifically)")

	// Default the service log to a file in the data dir so a normal run
	// (launched by l2p-poe with no --service-log/L2P_SERVICE_LOG override)
	// always leaves troubleshooting output on disk somewhere findable,
	// instead of going to a console nobody can see (l2p-poe.exe is a GUI
	// subsystem app with no attached console, so a spawned child's inherited
	// stdout/stderr is otherwise unobservable).
	defaultServiceLog := os.Getenv("L2P_SERVICE_LOG")
	if defaultServiceLog == "" {
		defaultServiceLog = filepath.Join(dataDir, "poe-info-service.log")
	}

	var installDirs installDirFlag
	flag.Var(&installDirs, "install-dir", "PoE install directory candidate (repeatable; first one found on disk wins, identifies the installs row)")
	var (
		logPath      = flag.String("log-path", "", "Exact path to Client.txt, bypassing install-dir resolution (dev convenience — see CONTRIBUTING.md)")
		port         = flag.Int("port", fileCfg.Port, "TCP port to listen on")
		bind         = flag.String("bind", fileCfg.Bind, "Bind address (default 127.0.0.1)")
		debugLogging = flag.Bool("debug-logging", fileCfg.DebugLogging, "Enable verbose debug logging")
		serviceLog   = flag.String("service-log", defaultServiceLog, "Path to service debug log file")
		idleTimeout  = flag.Duration("idle-timeout", server.DefaultIdleTimeout, "Shut down after this long with no client keep-alive or Client.txt activity")
		showVer      = flag.Bool("version", false, "Print version and exit")
	)
	flag.Parse()

	if *showVer {
		fmt.Println(proto.Version)
		return
	}

	resolvedDbPath := filepath.Join(dataDir, config.DBFileName)

	if *serviceLog != "" {
		f, err := os.OpenFile(*serviceLog, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err == nil {
			log.SetOutput(io.MultiWriter(os.Stderr, f))
			defer f.Close()
		} else {
			log.Printf("warn: cannot open service log %q: %v", *serviceLog, err)
		}
	}

	// Resolved after the service log redirect above so a skipped/stale
	// candidate is actually visible in the log file a user would check.
	installs := resolveInstallDirs(fileCfg.InstallDirs, installDirs, *logPath)

	cfg := server.Config{
		Version:              proto.Version,
		StartTime:            time.Now().Unix(),
		Bind:                 *bind,
		Port:                 *port,
		ConfigFilePath:       configFile,
		DebugLogging:         *debugLogging,
		Installs:             installs,
		AutoDetectInstallDir: fileCfg.AutoDetectInstallDir,
		ExecutableNames:      fileCfg.ExecutableNames,
		DbPath:               resolvedDbPath,
		IdleTimeout:          *idleTimeout,
	}

	log.Printf("starting v%s on %s:%d db=%q installs=%v",
		cfg.Version, cfg.Bind, cfg.Port, cfg.DbPath, cfg.Installs)

	if err := server.Run(cfg); err != nil {
		log.Fatalf("fatal: %v", err)
	}
}
