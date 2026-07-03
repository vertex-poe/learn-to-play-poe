package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"time"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/server"
)

func main() {
	if code, handled := cliDispatch(os.Args[1:]); handled {
		os.Exit(code)
	}

	exe, _ := os.Executable()
	configDir := config.ResolveDir(filepath.Dir(exe))
	fileCfg := config.Load(configDir)
	// Materialize a default poe-info-service.toml on first run, so the file
	// a human would want to inspect/edit during an outage always exists
	// (ADR-006) rather than only appearing after the first config.set call.
	if _, err := os.Stat(filepath.Join(configDir, config.FileName)); os.IsNotExist(err) {
		if err := config.Save(configDir, fileCfg); err != nil {
			log.Printf("warn: cannot write default config: %v", err)
		}
	}

	var (
		installDir   = flag.String("install-dir", "", "PoE install directory (identifies the installs row)")
		logPath      = flag.String("log-path", "", "Path to Client.txt (e.g. C:\\Games\\PoE\\logs\\Client.txt)")
		dbPath       = flag.String("db-path", "", "Path to poe-info-service's SQLite database (default: poe-info-service.db next to poe-info-service.toml)")
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

	resolvedDbPath := *dbPath
	if resolvedDbPath == "" {
		resolvedDbPath = filepath.Join(configDir, config.DBFileName)
	}

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
		Version:      proto.Version,
		StartTime:    time.Now().Unix(),
		Bind:         *bind,
		Port:         *port,
		ConfigDir:    configDir,
		DebugLogging: *debugLogging,
		InstallDir:   *installDir,
		LogPath:      *logPath,
		DbPath:       resolvedDbPath,
		ChannelNames: channelNames,
		IdleTimeout:  *idleTimeout,
	}

	log.Printf("starting v%s on %s:%d db=%q logPath=%q",
		cfg.Version, cfg.Bind, cfg.Port, cfg.DbPath, cfg.LogPath)

	if err := server.Run(cfg); err != nil {
		log.Fatalf("fatal: %v", err)
	}
}
