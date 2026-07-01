package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/server"
)

func main() {
	var (
		logPath  = flag.String("log-path", "", "Path to Client.txt (e.g. C:\\Games\\PoE\\logs\\Client.txt)")
		port     = flag.Int("port", 47652, "TCP port to listen on (127.0.0.1 only)")
		cacheDir = flag.String("cache-dir", defaultCacheDir(), "Directory for SQLite DB and state files")
		showVer  = flag.Bool("version", false, "Print version and exit")
	)
	flag.Parse()

	if *showVer {
		fmt.Println(proto.Version)
		return
	}

	cfg := server.Config{
		Version:   proto.Version,
		StartTime: time.Now().Unix(),
		Port:      *port,
		CacheDir:  *cacheDir,
		LogPath:   *logPath,
	}

	if err := server.Run(cfg); err != nil {
		log.Fatalf("fatal: %v", err)
		os.Exit(1)
	}
}

func defaultCacheDir() string {
	if appData := os.Getenv("APPDATA"); appData != "" {
		return filepath.Join(appData, "poe-info-service")
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".poe-info-service")
}
