package steam

import (
	"context"
	"time"

	"testing"
)

// testThrottleInterval is short relative to the real throttleInterval
// (250ms) so these tests run quickly, while still being long enough to
// reliably distinguish "waited" from "didn't wait" on a loaded CI runner.
const testThrottleInterval = 40 * time.Millisecond

func TestMinIntervalLimiterFirstCallDoesNotBlock(t *testing.T) {
	l := newMinIntervalLimiter(testThrottleInterval)
	start := time.Now()
	if err := l.Wait(context.Background()); err != nil {
		t.Fatalf("Wait: unexpected error: %v", err)
	}
	if elapsed := time.Since(start); elapsed > testThrottleInterval/2 {
		t.Errorf("first Wait took %s, want ~immediate", elapsed)
	}
}

func TestMinIntervalLimiterSecondCallIsDelayed(t *testing.T) {
	l := newMinIntervalLimiter(testThrottleInterval)
	ctx := context.Background()
	if err := l.Wait(ctx); err != nil {
		t.Fatalf("first Wait: unexpected error: %v", err)
	}

	start := time.Now()
	if err := l.Wait(ctx); err != nil {
		t.Fatalf("second Wait: unexpected error: %v", err)
	}
	if elapsed := time.Since(start); elapsed < testThrottleInterval/2 {
		t.Errorf("second Wait took %s, want >= ~%s", elapsed, testThrottleInterval)
	}
}

func TestMinIntervalLimiterRespectsContextCancellation(t *testing.T) {
	l := newMinIntervalLimiter(time.Hour) // long enough that only cancellation ends the wait
	if err := l.Wait(context.Background()); err != nil {
		t.Fatalf("first Wait: unexpected error: %v", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()

	start := time.Now()
	err := l.Wait(ctx)
	if err == nil {
		t.Fatal("Wait with a canceled context: want error, got nil")
	}
	if elapsed := time.Since(start); elapsed > 500*time.Millisecond {
		t.Errorf("Wait took %s to return after context deadline, want prompt return", elapsed)
	}
}
