//go:build !windows

package tailer

import "os"

// fileCreatedAt has no portable equivalent outside Windows; fall back to the
// modification time.
func fileCreatedAt(fi os.FileInfo) int64 {
	return fi.ModTime().Unix()
}
