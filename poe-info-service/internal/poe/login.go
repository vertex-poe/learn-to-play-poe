package poe

import (
	"context"
	"errors"
	"fmt"
)

// LoginOptions customizes a single Login attempt. The zero value uses
// production defaults; tests override OpenBrowser to simulate a user
// completing the flow instead of actually launching a browser.
type LoginOptions struct {
	// OpenBrowser defaults to the package-level OpenBrowser.
	OpenBrowser func(rawURL string) error
}

// Login runs one full interactive authorization: starts a loopback
// listener, opens the authorize URL in the system browser, waits for the
// redirect, validates the CSRF state, and exchanges the code for a token.
// See poe-apis.md §3.3 steps 1-9 and ADR-004 for why this needs no
// WebView-capable client.
func Login(ctx context.Context, client *Client, opts LoginOptions) (Token, error) {
	openBrowser := opts.OpenBrowser
	if openBrowser == nil {
		openBrowser = OpenBrowser
	}

	verifier, err := NewCodeVerifier()
	if err != nil {
		return Token{}, fmt.Errorf("generate code verifier: %w", err)
	}
	state, err := NewState()
	if err != nil {
		return Token{}, fmt.Errorf("generate state: %w", err)
	}
	challenge := CodeChallenge(verifier)

	ls, err := startLoopbackServer(CallbackPath)
	if err != nil {
		return Token{}, fmt.Errorf("start loopback listener: %w", err)
	}
	defer ls.Close()

	redirectURI := ls.redirectURI()
	authURL := client.AuthorizeURL(redirectURI, state, challenge)

	if err := openBrowser(authURL); err != nil {
		return Token{}, err
	}

	waitCtx, cancel := context.WithTimeout(ctx, loginTimeout)
	defer cancel()

	cb, err := ls.wait(waitCtx)
	if err != nil {
		return Token{}, fmt.Errorf("waiting for browser callback: %w", err)
	}
	if cb.oauthError != "" {
		return Token{}, fmt.Errorf("authorization denied: %s", cb.oauthError)
	}
	if cb.state != state {
		return Token{}, errors.New("state mismatch on callback: possible CSRF, aborting")
	}
	if cb.code == "" {
		return Token{}, errors.New("callback carried no authorization code")
	}

	return client.ExchangeCode(ctx, redirectURI, cb.code, verifier)
}
