//go:build !windows

package detect

// Process-table scanning is not implemented on this platform yet — this
// project currently only ships a Windows build (see internal/creds's same
// convention). Scan is a no-op elsewhere rather than an error so dev builds
// on other platforms simply never auto-detect.
func Scan(names []string) ([]string, error) {
	return nil, nil
}
