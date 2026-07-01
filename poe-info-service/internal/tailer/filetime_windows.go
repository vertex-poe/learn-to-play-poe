//go:build windows

package tailer

import (
	"os"
	"syscall"
)

// fileCreatedAt returns the file's creation time (birth time) as a Unix
// timestamp, mirroring QFileInfo::birthTime() on the old C++ side.
func fileCreatedAt(fi os.FileInfo) int64 {
	if attrs, ok := fi.Sys().(*syscall.Win32FileAttributeData); ok {
		return attrs.CreationTime.Nanoseconds() / int64(1_000_000_000)
	}
	return fi.ModTime().Unix()
}
