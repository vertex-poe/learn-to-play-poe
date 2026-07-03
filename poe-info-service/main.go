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

	var (
		installDir   = flag.String("install-dir", "", "PoE install directory (identifies the installs row)")
		logPath      = flag.String("log-path", "", "Path to Client.txt (e.g. C:\\Games\\PoE\\logs\\Client.txt)")
		configPath   = flag.String("config-path", "", "Path to l2p-poe's own config toml (for chat channel labels)")
		port         = flag.Int("port", fileCfg.Port, "TCP port to listen on")
		bind         = flag.String("bind", fileCfg.Bind, "Bind address (default 127.0.0.1)")
		debugLogging = flag.Bool("debug-logging", fileCfg.DebugLogging, "Enable verbose debug logging")
		serviceLog   = flag.String("service-log", os.Getenv("L2P_SERVICE_LOG"), "Path to service debug log file")
		idleTimeout  = flag.Duration("idle-timeout", server.DefaultIdleTimeout, "Shut down after this long with no client keep-alive or Client.txt activity")
		showVer      = flag.Bool("version", false, "Print version and exit")
	)
	flag.Parse()

	if *showVer {
		fmt.Println(proto.Version)
		return
	}

	resolvedDbPath := filepath.Join(dataDir, config.DBFileName)

	var channelNames map[int]string
	if *configPath != "" {
		channelNames = config.LoadChannelNames(*configPath)
	}

	if *serviceLog != "" {
		f, err := os.OpenFile(*serviceLog, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err == nil {
			log.SetOutput(io.MultiWriter(os.Stderr, f))
			defer f.Close()
		} else {
			log.Printf("warn: cannot open service log %q: %v", *serviceLog, err)
		}
	}

	cfg := server.Config{
		Version:        proto.Version,
		StartTime:      time.Now().Unix(),
		Bind:           *bind,
		Port:           *port,
		ConfigFilePath: configFile,
		DebugLogging:   *debugLogging,
		InstallDir:     *installDir,
		LogPath:        *logPath,
		DbPath:         resolvedDbPath,
		ChannelNames:   channelNames,
		IdleTimeout:    *idleTimeout,
	}

	log.Printf("starting v%s on %s:%d db=%q logPath=%q",
		cfg.Version, cfg.Bind, cfg.Port, cfg.DbPath, cfg.LogPath)

	if err := server.Run(cfg); err != nil {
		log.Fatalf("fatal: %v", err)
	}
}
