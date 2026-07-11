package steam

import (
	"context"
	"sync"
	"time"
)

// minIntervalLimiter enforces a minimum spacing between successive calls to
// Wait, blocking the caller only as long as needed since the previous call.
// One Client shares a single limiter across every outbound Steam request in
// a poll cycle (official + every scrape), matching the reference
// implementation's "sleep after every request, regardless of endpoint."
type minIntervalLimiter struct {
	mu       sync.Mutex
	interval time.Duration
	last     time.Time
}

func newMinIntervalLimiter(interval time.Duration) *minIntervalLimiter {
	return &minIntervalLimiter{interval: interval}
}

// Wait blocks until at least interval has elapsed since the previous call's
// return, or ctx is done — whichever comes first. The very first call never
// blocks.
func (l *minIntervalLimiter) Wait(ctx context.Context) error {
	l.mu.Lock()
	defer l.mu.Unlock()

	if !l.last.IsZero() {
		if wait := l.interval - time.Since(l.last); wait > 0 {
			timer := time.NewTimer(wait)
			defer timer.Stop()
			select {
			case <-timer.C:
			case <-ctx.Done():
				return ctx.Err()
			}
		}
	}
	l.last = time.Now()
	return nil
}
