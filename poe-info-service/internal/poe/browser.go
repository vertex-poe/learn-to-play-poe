package poe

import (
	"fmt"
	"os/exec"
	"runtime"
)

// OpenBrowser launches rawURL in the user's default system browser. Using
// the system browser (rather than an embedded WebView) is deliberate — per
// poe-apis.md §3.3, the user sees the genuine pathofexile.com URL and any
// existing login session is reused.
//
// Each command is invoked directly (never through a shell), so rawURL is
// passed as a single argv element and cannot be interpreted as shell syntax.
func OpenBrowser(rawURL string) error {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		// The well-established trick for opening a URL without going
		// through cmd.exe's "start" quoting quirks.
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", rawURL)
	case "darwin":
		cmd = exec.Command("open", rawURL)
	default:
		cmd = exec.Command("xdg-open", rawURL)
	}
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("open browser: %w", err)
	}
	return nil
}
