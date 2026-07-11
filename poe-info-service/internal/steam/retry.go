package steam

import (
	"context"
	"errors"
	"net/http"
	"time"
)

const (
	maxAttempts  = 3
	retryBackoff = 150 * time.Millisecond
)

// doWithRetry issues req (built fresh by newReq on every attempt, since a
// *http.Request's body can't be replayed after a failed attempt consumes
// it) up to maxAttempts times, retrying only on transport-level errors
// (connection reset, timeout, DNS failure — anything http.Client.Do itself
// returns an error for) and never on a non-2xx status, which callers must
// interpret themselves. Mirrors the reference implementation's
// makeWebRequest, narrowed to a bounded attempt count instead of an
// effectively-unbounded recursive retry.
func (c *Client) doWithRetry(ctx context.Context, newReq func(context.Context) (*http.Request, error)) (*http.Response, error) {
	var lastErr error
	for attempt := 0; attempt < maxAttempts; attempt++ {
		if attempt > 0 {
			timer := time.NewTimer(retryBackoff * time.Duration(attempt))
			select {
			case <-timer.C:
			case <-ctx.Done():
				timer.Stop()
				return nil, ctx.Err()
			}
		}

		if err := c.throttle.Wait(ctx); err != nil {
			return nil, err
		}

		req, err := newReq(ctx)
		if err != nil {
			return nil, err
		}
		resp, err := c.http.Do(req)
		if err == nil {
			return resp, nil
		}
		lastErr = err
		if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
			return nil, err
		}
	}
	return nil, lastErr
}
