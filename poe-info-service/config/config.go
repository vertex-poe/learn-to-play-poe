package config

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
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

// LoadChannelNames reads the [chat_channel_names] table (channel number ->
// user-defined label) from l2p-poe's own config toml, mirroring
// AppConfig::channelNames on the C++ side. Returns an empty map if the file
// is missing or the section is absent — channel labels are cosmetic, so any
// failure here is silent rather than fatal.
func LoadChannelNames(path string) map[int]string {
	names := map[int]string{}
	data, err := os.ReadFile(path)
	if err != nil {
		return names
	}

	inSection := false
	for _, raw := range strings.Split(string(data), "\n") {
		line := strings.TrimSpace(raw)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if strings.HasPrefix(line, "[") {
			inSection = line == "[chat_channel_names]"
			continue
		}
		if !inSection {
			continue
		}
		kv := strings.SplitN(line, "=", 2)
		if len(kv) != 2 {
			continue
		}
		num, err := strconv.Atoi(strings.TrimSpace(kv[0]))
		if err != nil {
			continue
		}
		names[num] = strings.Trim(strings.TrimSpace(kv[1]), `"`)
	}
	return names
}
