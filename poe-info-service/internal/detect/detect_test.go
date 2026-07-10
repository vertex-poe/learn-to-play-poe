package detect

import "testing"

func TestMatchesName(t *testing.T) {
	names := []string{"PathOfExile_x64Steam.exe", "PathOfExile.exe"}

	cases := []struct {
		exeName string
		want    bool
	}{
		{"PathOfExile.exe", true},
		{"pathofexile.exe", true}, // case-insensitive
		{"PATHOFEXILE_X64STEAM.EXE", true},
		{"notepad.exe", false},
		{"", false},
	}
	for _, c := range cases {
		if got := matchesName(c.exeName, names); got != c.want {
			t.Errorf("matchesName(%q, %v) = %v, want %v", c.exeName, names, got, c.want)
		}
	}
}

func TestInstallDir(t *testing.T) {
	cases := []struct {
		fullPath string
		want     string
	}{
		{`C:\Games\PoE\PathOfExile_x64Steam.exe`, `C:\Games\PoE`},
		{`D:\SteamLibrary\steamapps\common\Path of Exile\PathOfExile.exe`, `D:\SteamLibrary\steamapps\common\Path of Exile`},
	}
	for _, c := range cases {
		if got := installDir(c.fullPath); got != c.want {
			t.Errorf("installDir(%q) = %q, want %q", c.fullPath, got, c.want)
		}
	}
}
