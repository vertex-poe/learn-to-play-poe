package poe

import (
	"crypto/sha256"
	"encoding/base64"
	"testing"
)

func TestNewCodeVerifier_UniqueAndNonEmpty(t *testing.T) {
	a, err := NewCodeVerifier()
	if err != nil {
		t.Fatalf("NewCodeVerifier: %v", err)
	}
	b, err := NewCodeVerifier()
	if err != nil {
		t.Fatalf("NewCodeVerifier: %v", err)
	}
	if a == "" || b == "" {
		t.Fatal("NewCodeVerifier returned an empty string")
	}
	if a == b {
		t.Fatal("two calls to NewCodeVerifier returned the same value")
	}
}

func TestNewState_UniqueAndNonEmpty(t *testing.T) {
	a, err := NewState()
	if err != nil {
		t.Fatalf("NewState: %v", err)
	}
	b, err := NewState()
	if err != nil {
		t.Fatalf("NewState: %v", err)
	}
	if a == "" || b == "" {
		t.Fatal("NewState returned an empty string")
	}
	if a == b {
		t.Fatal("two calls to NewState returned the same value")
	}
}

// TestCodeChallenge_MatchesS256 proves CodeChallenge implements the exact
// BASE64URL(SHA256(verifier)) construction required by RFC 7636 — S256, not
// plain, is mandatory per poe-apis.md §3.3.
func TestCodeChallenge_MatchesS256(t *testing.T) {
	const verifier = "example-code-verifier-value-1234567890"
	sum := sha256.Sum256([]byte(verifier))
	want := base64.RawURLEncoding.EncodeToString(sum[:])

	got := CodeChallenge(verifier)
	if got != want {
		t.Errorf("CodeChallenge(%q) = %q, want %q", verifier, got, want)
	}
}

func TestCodeChallenge_Deterministic(t *testing.T) {
	const verifier = "same-verifier-every-time"
	if CodeChallenge(verifier) != CodeChallenge(verifier) {
		t.Error("CodeChallenge is not deterministic for the same verifier")
	}
}
