//go:build windows

package detect

import (
	"unsafe"

	"golang.org/x/sys/windows"
)

// Scan walks the current process table via a Toolhelp32 snapshot (no cgo,
// same no-cgo constraint as internal/creds — see ADR-005) and returns the
// deduplicated install directories of every running process whose
// executable basename case-insensitively matches one of names. Matching
// happens against the cheap Toolhelp32 exe name first, and the more
// expensive full-path resolution (OpenProcess + QueryFullProcessImageName)
// only runs for processes that already matched, mirroring the old C++
// WindowTracker's behavior without needing window enumeration at all.
func Scan(names []string) ([]string, error) {
	snapshot, err := windows.CreateToolhelp32Snapshot(windows.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return nil, err
	}
	defer windows.CloseHandle(snapshot)

	var entry windows.ProcessEntry32
	entry.Size = uint32(unsafe.Sizeof(entry))

	if err := windows.Process32First(snapshot, &entry); err != nil {
		return nil, err
	}

	seen := map[string]bool{}
	var dirs []string
	for {
		exeName := windows.UTF16ToString(entry.ExeFile[:])
		if matchesName(exeName, names) {
			if fullPath, ok := resolveImagePath(entry.ProcessID); ok {
				dir := installDir(fullPath)
				if !seen[dir] {
					seen[dir] = true
					dirs = append(dirs, dir)
				}
			}
		}
		if err := windows.Process32Next(snapshot, &entry); err != nil {
			break // ERROR_NO_MORE_FILES once the table is exhausted
		}
	}
	return dirs, nil
}

// resolveImagePath resolves a process's full executable path with the
// minimum privilege that allows it (PROCESS_QUERY_LIMITED_INFORMATION),
// consistent with the old C++ WindowTracker's OpenProcess call. Returns
// false for processes we can't open (e.g. elevated/protected processes) or
// that exit between the snapshot and this call — either way, silently
// skipped rather than treated as an error for the whole scan.
func resolveImagePath(pid uint32) (string, bool) {
	handle, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, pid)
	if err != nil {
		return "", false
	}
	defer windows.CloseHandle(handle)

	buf := make([]uint16, windows.MAX_PATH)
	size := uint32(len(buf))
	if err := windows.QueryFullProcessImageName(handle, 0, &buf[0], &size); err != nil {
		return "", false
	}
	return windows.UTF16ToString(buf[:size]), true
}
