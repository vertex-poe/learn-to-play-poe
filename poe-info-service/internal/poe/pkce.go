package poe

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
)

// randomURLSafeString returns n bytes of crypto/rand entropy, base64url
// (unpadded) encoded — used for both the PKCE code verifier and the CSRF
// state nonce, which have the same "high-entropy opaque string" requirement.
func randomURLSafeString(n int) (string, error) {
	buf := make([]byte, n)
	if _, err := rand.Read(buf); err != nil {
		return "", fmt.Errorf("generate random bytes: %w", err)
	}
	return base64.RawURLEncoding.EncodeToString(buf), nil
}

// NewCodeVerifier returns a fresh, high-entropy PKCE code verifier.
func NewCodeVerifier() (string, error) {
	return randomURLSafeString(32)
}

// CodeChallenge derives the S256 PKCE code challenge from a verifier:
// BASE64URL(SHA256(verifier)). S256 is mandatory — plain is never used, per
// _reference/poe-apis/poe-apis.md §3.3 and the project's public-client
// posture (no embedded secret to protect with a weaker method).
func CodeChallenge(verifier string) string {
	sum := sha256.Sum256([]byte(verifier))
	return base64.RawURLEncoding.EncodeToString(sum[:])
}

// NewState returns a fresh, high-entropy CSRF nonce for the authorize
// request, to be compared against the value the loopback callback receives.
func NewState() (string, error) {
	return randomURLSafeString(16)
}
