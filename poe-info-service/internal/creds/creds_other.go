//go:build !windows

package creds

import "errors"

// macOS (keybase/go-keychain) and Linux (godbus/dbus Secret Service) backends
// are not implemented yet — this project currently only ships a Windows
// build. See poe-info-service/docs/decisions/005-credential-storage-mechanism.md.

var errUnsupported = errors.New("credential storage is not implemented on this platform yet")

func Store(service, key, value string) error {
	return errUnsupported
}

func Get(service, key string) (string, error) {
	return "", errUnsupported
}

func Delete(service, key string) error {
	return errUnsupported
}
