package main

import (
	"path/filepath"
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/server"
)

func TestResolveInstallDirsSkipsMissingCandidates(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "does-not-exist")
	real := t.TempDir()

	got := resolveInstallDirs(nil, []string{missing, real}, "")

	want := []server.InstallTarget{{Dir: real, LogPath: filepath.Join(real, "logs", "Client.txt")}}
	if !equalTargets(got, want) {
		t.Errorf("expected only the existing candidate %v, got %v", want, got)
	}
}

func TestResolveInstallDirsIngestsEveryExistingCandidate(t *testing.T) {
	// The whole point of this function: don't stop at the first hit. A user
	// with two real, simultaneously-valid PoE installs (e.g. two SteamLibrary
	// drives) must have both ingested, not just whichever is listed first.
	missing := filepath.Join(t.TempDir(), "does-not-exist")
	real1 := t.TempDir()
	real2 := t.TempDir()

	got := resolveInstallDirs(nil, []string{real1, missing, real2}, "")

	want := []server.InstallTarget{
		{Dir: real1, LogPath: filepath.Join(real1, "logs", "Client.txt")},
		{Dir: real2, LogPath: filepath.Join(real2, "logs", "Client.txt")},
	}
	if !equalTargets(got, want) {
		t.Errorf("expected both existing candidates in order %v, got %v", want, got)
	}
}

func TestResolveInstallDirsAcceptsDirWithoutClientLog(t *testing.T) {
	// A fresh, valid install nobody has launched yet has no Client.txt —
	// resolveInstallDirs must only check the directory, not the log file.
	real := t.TempDir()

	got := resolveInstallDirs(nil, []string{real}, "")

	want := []server.InstallTarget{{Dir: real, LogPath: filepath.Join(real, "logs", "Client.txt")}}
	if !equalTargets(got, want) {
		t.Errorf("expected install dir %v (no Client.txt yet) to still be selected, got %v", want, got)
	}
}

func TestResolveInstallDirsAllMissingYieldsEmpty(t *testing.T) {
	missing1 := filepath.Join(t.TempDir(), "gone1")
	missing2 := filepath.Join(t.TempDir(), "gone2")

	got := resolveInstallDirs(nil, []string{missing1, missing2}, "")

	if len(got) != 0 {
		t.Errorf("expected no install targets when nothing exists, got %v", got)
	}
}

func TestResolveInstallDirsNoCandidatesYieldsEmpty(t *testing.T) {
	got := resolveInstallDirs(nil, nil, "")

	if len(got) != 0 {
		t.Errorf("expected no install targets with no candidates, got %v", got)
	}
}

func TestResolveInstallDirsExplicitLogPathBypassesSearch(t *testing.T) {
	// Dev convenience (CONTRIBUTING.md): an explicit --log-path is honored
	// as-is, as the sole target, even if it doesn't match any configured
	// install dir and even if other candidates also exist.
	got := resolveInstallDirs(nil, []string{"C:/Games/PoE", "C:/Games/PoE2"}, "D:/elsewhere/Client.txt")

	want := []server.InstallTarget{{Dir: "C:/Games/PoE", LogPath: "D:/elsewhere/Client.txt"}}
	if !equalTargets(got, want) {
		t.Errorf("expected explicit logPath to be used as-is with the first candidate, got %v", got)
	}
}

func TestResolveInstallDirsExplicitLogPathWithNoCandidates(t *testing.T) {
	got := resolveInstallDirs(nil, nil, "D:/elsewhere/Client.txt")

	want := []server.InstallTarget{{Dir: "", LogPath: "D:/elsewhere/Client.txt"}}
	if !equalTargets(got, want) {
		t.Errorf("expected explicit logPath to be used as-is with an empty install dir, got %v", got)
	}
}

func TestResolveInstallDirsMergesPersistedAndFlagCandidates(t *testing.T) {
	persisted := t.TempDir()
	flagged := t.TempDir()

	got := resolveInstallDirs([]string{persisted}, []string{flagged}, "")

	want := []server.InstallTarget{
		{Dir: persisted, LogPath: filepath.Join(persisted, "logs", "Client.txt")},
		{Dir: flagged, LogPath: filepath.Join(flagged, "logs", "Client.txt")},
	}
	if !equalTargets(got, want) {
		t.Errorf("expected persisted dirs before flag dirs %v, got %v", want, got)
	}
}

func TestResolveInstallDirsDedupesPersistedAndFlagCandidates(t *testing.T) {
	// A --install-dir flag repeating what's already in poe-info-service.toml
	// (e.g. a dev harness that always passes it explicitly) must not start a
	// second tailer for the same directory.
	shared := t.TempDir()

	got := resolveInstallDirs([]string{shared}, []string{shared}, "")

	want := []server.InstallTarget{{Dir: shared, LogPath: filepath.Join(shared, "logs", "Client.txt")}}
	if !equalTargets(got, want) {
		t.Errorf("expected the duplicate to be deduped to one target %v, got %v", want, got)
	}
}

func equalTargets(a, b []server.InstallTarget) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
