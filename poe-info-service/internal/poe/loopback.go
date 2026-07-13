package poe

import (
	"context"
	"fmt"
	"net"
	"net/http"
)

// loopbackCallback is what the loopback listener captured from the
// authorization server's redirect: either code+state, or an OAuth error
// parameter (e.g. "access_denied" if the user declined consent).
type loopbackCallback struct {
	code  string
	state string
	oauthError string
}

// loopbackServer is a short-lived HTTP server bound to 127.0.0.1 on an
// OS-assigned port, per the loopback interface redirect pattern (RFC 8252)
// described in _reference/poe-apis/poe-apis.md §3.3 — no external
// infrastructure or pre-registered redirect URL is needed, and the dynamic
// port avoids port-conflict failures across concurrent login attempts.
type loopbackServer struct {
	ln     net.Listener
	srv    *http.Server
	result chan loopbackCallback
}

// startLoopbackServer binds a new listener on 127.0.0.1 and starts serving
// path, capturing the first request's code/state/error query parameters.
func startLoopbackServer(path string) (*loopbackServer, error) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return nil, fmt.Errorf("bind loopback listener: %w", err)
	}

	l := &loopbackServer{
		ln:     ln,
		result: make(chan loopbackCallback, 1),
	}

	mux := http.NewServeMux()
	mux.HandleFunc(path, l.handleCallback)
	l.srv = &http.Server{Handler: mux}
	go l.srv.Serve(ln)

	return l, nil
}

// handleCallback captures the redirect's query parameters and returns a
// minimal human-readable page — the browser tab is no longer needed once
// this fires, per poe-apis.md's step 6.
func (l *loopbackServer) handleCallback(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	cb := loopbackCallback{
		code:       q.Get("code"),
		state:      q.Get("state"),
		oauthError: q.Get("error"),
	}

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if cb.oauthError != "" {
		fmt.Fprintf(w, "<html><body>Authorization failed (%s). You may close this window.</body></html>", cb.oauthError)
	} else {
		fmt.Fprint(w, "<html><body>Authorization complete. You may close this window.</body></html>")
	}

	select {
	case l.result <- cb:
	default:
		// A second/duplicate hit (e.g. browser retrying, or a favicon
		// request landing on the same handler) after the first callback was
		// already delivered — ignored rather than blocking the handler.
	}
}

// redirectURI is the full loopback redirect URI, including the dynamically
// assigned port, to send as redirect_uri at both the authorize and
// token-exchange steps.
func (l *loopbackServer) redirectURI() string {
	return fmt.Sprintf("http://127.0.0.1:%d%s", l.ln.Addr().(*net.TCPAddr).Port, CallbackPath)
}

// wait blocks until the callback is captured or ctx is done.
func (l *loopbackServer) wait(ctx context.Context) (loopbackCallback, error) {
	select {
	case cb := <-l.result:
		return cb, nil
	case <-ctx.Done():
		return loopbackCallback{}, ctx.Err()
	}
}

// Close shuts down the listener. Safe to call once the callback has been
// captured (or the wait has given up) — the listener is only needed for the
// single redirect, per poe-apis.md's step 11.
func (l *loopbackServer) Close() error {
	return l.srv.Close()
}
