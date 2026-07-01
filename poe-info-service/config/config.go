package config

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const (
	DefaultBind = "127.0.0.1"
	DefaultPort = 47652
)

type Config struct {
	Bind string
	Port int
}

// Load reads poe-info-service.toml from dir, returning hardcoded defaults for
// any key that is absent or unparseable.
func Load(dir string) Config {
	cfg := Config{Bind: DefaultBind, Port: DefaultPort}
	data, err := os.ReadFile(filepath.Join(dir, "poe-info-service.toml"))
	if err != nil {
		return cfg
	}
	for _, raw := range strings.Split(string(data), "\n") {
		line := strings.TrimSpace(raw)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		kv := strings.SplitN(line, "=", 2)
		if len(kv) != 2 {
			continue
		}
		key := strings.TrimSpace(kv[0])
		val := strings.Trim(strings.TrimSpace(kv[1]), `"`)
		switch key {
		case "bind":
			cfg.Bind = val
		case "port":
			fmt.Sscanf(val, "%d", &cfg.Port)
		}
	}
	return cfg
}
