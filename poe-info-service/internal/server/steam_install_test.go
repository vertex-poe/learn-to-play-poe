package server

import "testing"

func TestIsSteamInstall_PathHeuristic(t *testing.T) {
	tests := []struct {
		name string
		dir  string
		want bool
	}{
		{name: "steam library path", dir: `D:\SteamLibrary\steamapps\common\Path of Exile`, want: true},
		{name: "steam library path lowercase", dir: `/home/user/.steam/steamapps/common/Path of Exile`, want: true},
		{name: "standalone GGG install", dir: `C:\Games\Path of Exile`, want: false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// No Steam-flavored exec names configured, so only the path
			// heuristic can fire — isolates it from the process-scan branch,
			// which needs a real (or non-existent, platform-dependent)
			// process table.
			if got := isSteamInstall(tt.dir, nil); got != tt.want {
				t.Errorf("isSteamInstall(%q, nil) = %v, want %v", tt.dir, got, tt.want)
			}
		})
	}
}

func TestIsSteamInstall_NoSteamExecNamesConfigured(t *testing.T) {
	// A non-steamapps path with only non-Steam-flavored exec names
	// configured must not fall through to a process scan at all.
	execNames := []string{"PathOfExile_x64.exe", "PathOfExile.exe"}
	if got := isSteamInstall(`C:\Games\Path of Exile`, execNames); got {
		t.Error("isSteamInstall with no Steam-flavored exec names configured = true, want false")
	}
}
