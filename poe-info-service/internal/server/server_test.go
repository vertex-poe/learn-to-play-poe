package server

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
)

// waitForCancel blocks until ctx is done or the timeout elapses, returning
// whether ctx was actually cancelled.
func waitForCancel(ctx context.Context, timeout time.Duration) bool {
	select {
	case <-ctx.Done():
		return true
	case <-time.After(timeout):
		return false
	}
}

func TestWatchIdleShutsDownAfterTimeout(t *testing.T) {
	srv := &server{}
	srv.touch()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, nil, 30*time.Millisecond, 5*time.Millisecond)

	if !waitForCancel(ctx, time.Second) {
		t.Fatal("expected watchIdle to cancel the context after the idle timeout, but it did not")
	}
}

func TestWatchIdleResetsOnClientActivity(t *testing.T) {
	srv := &server{}
	srv.touch()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, nil, 200*time.Millisecond, 10*time.Millisecond)

	// Keep touching the server well inside the idle timeout so it never fires,
	// with generous margin for scheduling jitter.
	deadline := time.Now().Add(400 * time.Millisecond)
	for time.Now().Before(deadline) {
		srv.touch()
		time.Sleep(20 * time.Millisecond)
	}

	select {
	case <-ctx.Done():
		t.Fatal("watchIdle shut down despite ongoing client activity")
	default:
	}
}

// fakeTailerActivity stands in for tailer.Tailer.LastActivity in tests,
// letting us simulate Client.txt activity without a real log file.
type fakeTailerActivity struct {
	mu   sync.Mutex
	last time.Time
}

func (f *fakeTailerActivity) touch() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.last = time.Now()
}

func (f *fakeTailerActivity) LastActivity() time.Time {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.last
}

func TestWatchIdleKeptAliveByTailerActivity(t *testing.T) {
	srv := &server{}
	// Simulate a client that connected a long time ago and went idle.
	srv.lastActivity.Store(time.Now().Add(-time.Hour).UnixNano())

	activity := &fakeTailerActivity{}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, activity.LastActivity, 200*time.Millisecond, 10*time.Millisecond)

	deadline := time.Now().Add(400 * time.Millisecond)
	for time.Now().Before(deadline) {
		activity.touch()
		time.Sleep(20 * time.Millisecond)
	}

	select {
	case <-ctx.Done():
		t.Fatal("watchIdle shut down despite ongoing Client.txt activity")
	default:
	}
}

func TestRouteMessagePingDoesNotTouch(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()

	srv.routeMessage(c, proto.Message{Type: proto.TypePing, ID: "1"})

	if srv.lastActivity.Load() != 0 {
		t.Fatal("expected a bare ping not to touch lastActivity")
	}
}

func TestRouteMessageKeepaliveTouches(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()

	before := time.Now()
	srv.routeMessage(c, proto.Message{Type: proto.TypeKeepalive, ID: "1"})

	got := srv.lastActivity.Load()
	if got == 0 {
		t.Fatal("expected keepalive to touch lastActivity")
	}
	if time.Unix(0, got).Before(before) {
		t.Fatalf("lastActivity %v looks stale relative to %v", time.Unix(0, got), before)
	}
}

func TestRouteMessageSubscribeTouches(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()

	srv.routeMessage(c, proto.Message{Type: proto.TypeSubscribe, Topic: "clientlog", ID: "1"})

	if srv.lastActivity.Load() == 0 {
		t.Fatal("expected subscribe to touch lastActivity")
	}
}

func TestWatchIdleFiresWhenTailerGoesQuiet(t *testing.T) {
	srv := &server{}
	srv.lastActivity.Store(time.Now().Add(-time.Hour).UnixNano())

	activity := &fakeTailerActivity{}
	activity.touch() // one recent burst, then silence

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, activity.LastActivity, 30*time.Millisecond, 5*time.Millisecond)

	if !waitForCancel(ctx, time.Second) {
		t.Fatal("expected watchIdle to cancel once tailer activity also went stale")
	}
}
