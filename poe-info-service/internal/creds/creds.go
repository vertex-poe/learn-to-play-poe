// Package creds stores secrets (POESESSID, future OAuth tokens) directly in
// the operating system's native credential store rather than this service's
// own SQLite database. poe-info-service is the sole reader/writer of this
// storage, and it never hands a stored value back to a WebSocket client — see
// poe-info-service/docs/decisions/004-credential-custody.md and
// 005-credential-storage-mechanism.md.
package creds

import "errors"

// ErrNotFound is returned by Get when no credential is stored under the
// given service/key pair, regardless of which platform backend is in use.
var ErrNotFound = errors.New("credential not found")

// ServiceName is the fixed identifier every credential is stored under, per
// ADR-005 ("a fixed service name plus a per-credential-type key"), so
// storage is scoped to poe-info-service itself rather than to whichever
// addon happened to supply the value.
const ServiceName = "poe-info-service"

// Store, Get, and Delete are implemented per-platform in creds_windows.go
// and creds_other.go.
