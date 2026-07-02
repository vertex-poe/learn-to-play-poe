//go:build windows

package creds

import (
	"errors"
	"fmt"

	"github.com/danieljoos/wincred"
)

// Windows Credential Manager, via CredWrite/CredRead syscalls — no cgo
// required, per ADR-005.

func targetName(service, key string) string {
	return service + "/" + key
}

func Store(service, key, value string) error {
	cred := wincred.NewGenericCredential(targetName(service, key))
	cred.CredentialBlob = []byte(value)
	if err := cred.Write(); err != nil {
		return fmt.Errorf("wincred: write %q: %w", key, err)
	}
	return nil
}

func Get(service, key string) (string, error) {
	cred, err := wincred.GetGenericCredential(targetName(service, key))
	if err != nil {
		if errors.Is(err, wincred.ErrElementNotFound) {
			return "", ErrNotFound
		}
		return "", fmt.Errorf("wincred: read %q: %w", key, err)
	}
	return string(cred.CredentialBlob), nil
}

func Delete(service, key string) error {
	cred, err := wincred.GetGenericCredential(targetName(service, key))
	if err != nil {
		if errors.Is(err, wincred.ErrElementNotFound) {
			return nil // already gone
		}
		return fmt.Errorf("wincred: read %q before delete: %w", key, err)
	}
	if err := cred.Delete(); err != nil {
		return fmt.Errorf("wincred: delete %q: %w", key, err)
	}
	return nil
}
