// Package detect finds running Path of Exile install directories by
// scanning currently running processes for a match against a configured
// set of executable basenames. This is the Go-side successor to l2p-poe's
// old WindowTracker-based auto-detection: it keeps the "derive install dir
// from a running game process" behavior, but drops the window-rect/HWND
// bookkeeping that WindowTracker also did, which stays client-side in
// l2p-poe for overlay positioning and is unrelated to install-dir detection.
package detect

import (
	"path/filepath"
	"strings"
)

// matchesName reports whether exeName case-insensitively equals one of
// names — pulled out as its own pure function so the matching logic is
// unit-testable without a real process table.
func matchesName(exeName string, names []string) bool {
	for _, n := range names {
		if strings.EqualFold(exeName, n) {
			return true
		}
	}
	return false
}

// installDir returns the directory containing fullPath — the value Scan
// records for a matched process's executable.
func installDir(fullPath string) string {
	return filepath.Dir(fullPath)
}
