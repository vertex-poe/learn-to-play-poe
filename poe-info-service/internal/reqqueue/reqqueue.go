// Package reqqueue is a generic, reusable, rate-limit-aware priority queue
// for outbound API requests. It exists to satisfy
// _reference/poe-apis/poe-apis.md §5.5's "Queue-Per-Policy Architecture" —
// one serial pacing gate per rate-limit policy, computed from the live
// X-Rate-Limit-*-State response headers each API publishes — without
// hard-coding which API it's for. Per an architecture review (Opus,
// 2026-07-13), a Queue is meant to be instantiated once per API (one for
// the PoE OAuth API, a separate one whenever the PoE Legacy API's own
// requests need the same treatment) rather than shared across APIs: they
// hit different hosts with independent rate-limit budgets that never
// constrain each other, so separate instances of this one reusable type
// avoid coupling two things that don't actually interact.
//
// The queue itself never builds or authenticates HTTP requests — that stays
// with the caller (Task.Exec), which keeps this package ignorant of
// per-API auth schemes (Bearer token vs. POESESSID cookie) and free to
// perform whatever side effects it wants (caching a result, publishing a
// WebSocket topic) as part of running a task. The one piece that
// necessarily differs per API is how to parse a policy's name and rule
// state out of that API's response headers (poe-apis.md §5.1: OAuth uses
// named X-Rate-Limit-Policy/-Rules headers, the legacy API uses fixed
// Ip/Account bucket names) — that's the single HeaderParser seam callers
// supply.
//
// Known simplifications in this first slice (see ROADMAP.md):
//   - No proactive HEAD-request policy discovery (poe-apis.md §5.4): a
//     brand-new Task whose PolicyHint is empty or not yet known dispatches
//     ungated the first time, exactly like a real never-before-seen
//     endpoint would. Only once dispatch has actually seen a response is a
//     policy's pacing enforced.
//   - The delay computed once a policy is saturated is that rule's full
//     Period (a safe, if not maximally efficient, upper bound), not the
//     exact "time until this rolling window resets" computation poe-apis.md
//     §5.6 describes — that needs a request-timestamp history this package
//     doesn't keep yet.
package reqqueue

import (
	"context"
	"errors"
	"net/http"
	"sync"
	"time"
)

// Priority levels a Task may be submitted or promoted/demoted to. Integer
// comparison drives scheduling — higher runs first among tasks whose
// policy is currently clear to dispatch. Gaps are left deliberately (mirrors
// src/core/DeferredTaskQueue.h's l2p-poe UI preload queue) so future levels
// can be inserted without renumbering existing callers.
const (
	PriorityLow       = 1
	PriorityMedium    = 2
	PriorityHigh      = 3
	PriorityImmediate = 10
)

// rateLimitBuffer is added to every computed delay to absorb the server's
// bucket boundaries not being perfectly aligned with this client's clock —
// poe-apis.md §5.3's documented buffer.
const rateLimitBuffer = 1 * time.Second

// ErrCancelled is Waiter.Wait's error when the task was removed via Cancel
// before it was dispatched.
var ErrCancelled = errors.New("reqqueue: task cancelled")

// Rule is one rate-limit rule as poe-apis.md §5 describes it: Hits allowed
// within Period, and how long a violation is restricted for, plus the
// current count (StateHits) from the paired "-State" header. A single named
// policy commonly carries more than one Rule (a fast/burst tier and a
// slow/sustained tier, poe-apis.md §5.3) — all of them gate the same
// policy's next dispatch; the most restrictive currently-saturated one
// wins.
type Rule struct {
	Name        string
	Hits        int
	Period      time.Duration
	Restriction time.Duration
	StateHits   int
}

// HeaderParser extracts a completed response's rate-limit policy name and
// rule state, per poe-apis.md §5.1 — the sole seam that differs between the
// PoE OAuth API's named-policy headers and the PoE Legacy API's fixed
// Ip/Account bucket headers. ok is false when the response carries no
// rate-limit headers at all (e.g. a request that errored before reaching
// the rate-limited handler), in which case the queue doesn't touch policy
// state for that dispatch.
type HeaderParser func(h http.Header) (policyKey string, rules []Rule, ok bool)

// Task is one request to run through a Queue.
type Task struct {
	// Key de-duplicates: submitting the same Key while it's still queued or
	// in flight merges into the existing task (promoting its priority to
	// the max of the two, and handing back a Waiter on the same in-flight
	// result) rather than dispatching a second time. Once the task
	// completes, Key is free to be submitted again (e.g. a caller wanting a
	// fresh fetch after cache expiry).
	Key string

	// Priority — see the Priority* constants. Ties are broken FIFO by
	// submission order.
	Priority int

	// PolicyHint is this task's best-known rate-limit policy key before any
	// response has actually confirmed it — e.g. a caller who already knows
	// (from prior observation, or poe-apis.md) which named policy an
	// endpoint reports can pass that name directly so a burst of
	// first-ever calls to related endpoints still paces together rather
	// than each dispatching ungated. Leave empty if unknown; the task then
	// simply dispatches ungated until a response reveals the real policy
	// (poe-apis.md §5.4's actual behavior for a never-before-seen
	// endpoint). Whatever a response's HeaderParser actually reports always
	// wins going forward, whether or not it matches this hint.
	PolicyHint string

	// Exec performs the actual HTTP call — already fully authenticated by
	// the caller — and returns the decoded result plus the response's raw
	// headers (so the queue can learn/update this task's policy's
	// rate-limit state via HeaderParser), or an error. The queue never
	// builds or authenticates requests itself, and doesn't know or care
	// what side effects Exec performs (caching a result, publishing a
	// WebSocket topic) beyond its return value.
	Exec func(ctx context.Context) (result any, headers http.Header, err error)
}

type policyState struct {
	rules         []Rule
	nextAllowedAt time.Time
}

type entry struct {
	task     Task
	queuedAt time.Time
	done     chan struct{}
	result   any
	err      error
}

// Waiter is returned by Submit. A caller uninterested in blocking can simply
// discard it — the task still runs and completes via whatever side effects
// its Exec performs; Wait is only needed by a caller that wants the result
// delivered inline.
type Waiter struct{ e *entry }

// Wait blocks until the task completes or ctx is done, whichever comes
// first. A ctx timing out or being cancelled (e.g. the requesting
// connection disconnected) only abandons *this* wait — the underlying task
// keeps running to completion in the background regardless, so its result
// is still available to any other current or future Waiter on the same Key
// (while still queued/in flight) or from whatever cache/publish side effect
// its Exec performs.
func (w *Waiter) Wait(ctx context.Context) (any, error) {
	select {
	case <-w.e.done:
		return w.e.result, w.e.err
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// Queue is a rate-limit-aware, priority-ordered, de-duplicating request
// scheduler for one API. Construct one per API with New; do not share a
// single Queue across APIs with independent rate-limit budgets (see the
// package doc comment).
type Queue struct {
	parse HeaderParser

	mu         sync.Mutex
	byKey      map[string]*entry // every not-yet-completed task, queued or in flight
	pending    []*entry          // not yet dispatched; subset of byKey's values
	policies   map[string]*policyState
	hintPolicy map[string]string // PolicyHint -> the real policy key a response has since revealed it maps to
	inFlight   map[string]bool   // PolicyHint -> a dispatch for that policy is currently running

	wake chan struct{}
}

// New returns a running Queue. parse is this API's HeaderParser (see its
// doc comment). ctx bounds the queue's entire lifetime: cancelling it stops
// the dispatch loop and every future Exec call is made with ctx already
// done (callers should stop Submitting once ctx is cancelled — in-flight
// Exec calls are responsible for respecting ctx themselves).
func New(ctx context.Context, parse HeaderParser) *Queue {
	q := &Queue{
		parse:      parse,
		byKey:      make(map[string]*entry),
		policies:   make(map[string]*policyState),
		hintPolicy: make(map[string]string),
		inFlight:   make(map[string]bool),
		wake:       make(chan struct{}, 1),
	}
	go q.run(ctx)
	return q
}

// Submit enqueues task, or merges into an existing not-yet-completed task
// with the same Key (promoting its priority to the max of the two), and
// returns a Waiter either way.
func (q *Queue) Submit(task Task) *Waiter {
	q.mu.Lock()
	defer q.mu.Unlock()

	if e, ok := q.byKey[task.Key]; ok {
		if task.Priority > e.task.Priority {
			e.task.Priority = task.Priority
		}
		return &Waiter{e: e}
	}

	e := &entry{task: task, queuedAt: time.Now(), done: make(chan struct{})}
	q.byKey[task.Key] = e
	q.pending = append(q.pending, e)
	q.signal()
	return &Waiter{e: e}
}

// SetPriority forces key's priority directly, bypassing Submit's
// merge-to-max behavior — intended for interaction-driven promotion/demotion
// of an already-queued task (e.g. a UI panel becoming visible/hidden). A
// no-op if key isn't currently queued or in flight.
func (q *Queue) SetPriority(key string, priority int) {
	q.mu.Lock()
	defer q.mu.Unlock()
	if e, ok := q.byKey[key]; ok {
		e.task.Priority = priority
		q.signal()
	}
}

// Cancel removes key's not-yet-dispatched queue slot. A no-op if key is
// already dispatching or not present at all — an in-flight request is never
// aborted, since its result may still be useful to whoever else is waiting
// on it or to a cache a completed fetch would populate.
func (q *Queue) Cancel(key string) {
	q.mu.Lock()
	defer q.mu.Unlock()
	for i, e := range q.pending {
		if e.task.Key == key {
			q.pending = append(q.pending[:i], q.pending[i+1:]...)
			delete(q.byKey, key)
			e.err = ErrCancelled
			close(e.done)
			return
		}
	}
}

func (q *Queue) signal() {
	select {
	case q.wake <- struct{}{}:
	default:
	}
}

// run is the queue's sole dispatch loop: repeatedly pick the
// highest-priority task whose policy is currently clear to dispatch,
// spawn it, and immediately look for the next one (a different policy may
// already be clear too) — only blocking when nothing is currently
// dispatchable.
func (q *Queue) run(ctx context.Context) {
	for {
		q.mu.Lock()
		e, wait := q.pickNext()
		q.mu.Unlock()

		if e != nil {
			go q.dispatch(ctx, e)
			continue
		}

		select {
		case <-q.wake:
		case <-time.After(wait):
		case <-ctx.Done():
			return
		}
	}
}

// pickNext returns the highest-priority pending task whose policy is
// currently clear to dispatch (ties broken by earliest submission), and
// removes it from q.pending — or nil and how long until the
// soonest-blocked task's policy clears, if nothing is ready yet. Must be
// called with q.mu held.
//
// A task whose PolicyHint already has a dispatch in flight is never
// considered ready, regardless of what its computed delay says — per
// poe-apis.md §5.5, requests sharing a policy are dispatched serially (send
// one, wait for its reply, only then send the next), not just paced by a
// computed delay that could let two same-policy requests race each other
// as concurrent dispatches. Such a task contributes no "soonest" wait,
// since there's no fixed ETA for it — dispatch() signals the queue when
// the in-flight one completes and frees its policy up.
func (q *Queue) pickNext() (*entry, time.Duration) {
	now := time.Now()
	best := -1
	var soonest time.Duration
	haveSoonest := false

	for i, e := range q.pending {
		if e.task.PolicyHint != "" && q.inFlight[e.task.PolicyHint] {
			continue
		}
		wait := q.policyWait(e.task.PolicyHint, now)
		if wait <= 0 {
			if best == -1 ||
				e.task.Priority > q.pending[best].task.Priority ||
				(e.task.Priority == q.pending[best].task.Priority && e.queuedAt.Before(q.pending[best].queuedAt)) {
				best = i
			}
			continue
		}
		if !haveSoonest || wait < soonest {
			soonest, haveSoonest = wait, true
		}
	}

	if best == -1 {
		if !haveSoonest {
			return nil, time.Hour // nothing pending at all (or blocked only on in-flight policies); wait for a wake signal
		}
		return nil, soonest
	}

	e := q.pending[best]
	q.pending = append(q.pending[:best], q.pending[best+1:]...)
	if e.task.PolicyHint != "" {
		q.inFlight[e.task.PolicyHint] = true
	}
	return e, 0
}

// policyWait reports how long until hint's policy is clear to dispatch
// again, or 0 if it's clear now (including when hint is empty/unknown — see
// the package doc comment on not proactively discovering policies). hint is
// resolved through hintPolicy first: a response may have revealed that this
// hint's real rate-limit policy is named something else entirely (the
// common case — a caller's PolicyHint is a best guess or a stable grouping
// label, not necessarily the server's actual policy name), in which case
// the learned name is what's actually gating future dispatches, not the
// hint string itself. Must be called with q.mu held.
func (q *Queue) policyWait(hint string, now time.Time) time.Duration {
	if hint == "" {
		return 0
	}
	key := hint
	if learned, ok := q.hintPolicy[hint]; ok {
		key = learned
	}
	ps, ok := q.policies[key]
	if !ok || !ps.nextAllowedAt.After(now) {
		return 0
	}
	return ps.nextAllowedAt.Sub(now)
}

// dispatch runs e's task, learns/updates whatever policy its response
// reports (recording the mapping from e.task.PolicyHint to that policy's
// real key so future same-hint tasks are correctly paced by it — see
// policyWait), releases e.task.PolicyHint's in-flight marker (letting the
// next queued task under the same policy become dispatchable), and
// delivers the result to every current and future Waiter on e.
func (q *Queue) dispatch(ctx context.Context, e *entry) {
	result, headers, err := e.task.Exec(ctx)

	if headers != nil {
		if key, rules, ok := q.parse(headers); ok && key != "" {
			q.mu.Lock()
			q.policies[key] = &policyState{rules: rules, nextAllowedAt: time.Now().Add(computeDelay(rules))}
			if e.task.PolicyHint != "" {
				q.hintPolicy[e.task.PolicyHint] = key
			}
			q.mu.Unlock()
		}
	}

	q.mu.Lock()
	delete(q.byKey, e.task.Key)
	if e.task.PolicyHint != "" {
		delete(q.inFlight, e.task.PolicyHint)
	}
	q.mu.Unlock()

	e.result, e.err = result, err
	close(e.done)
	q.signal()
}

// computeDelay is poe-apis.md §5.6's minimum-safe-delay calculation,
// simplified: for each rule currently at or over capacity (StateHits >=
// Hits), the safe wait is that rule's full Period — a conservative upper
// bound on "time until the bucket resets" rather than the exact remaining
// time (which would need a rolling history of request timestamps this
// package doesn't keep — see the package doc comment). The actual delay is
// the maximum across every saturated rule (the most restrictive one
// governs), plus the §5.3 buffer.
func computeDelay(rules []Rule) time.Duration {
	var maxWait time.Duration
	for _, r := range rules {
		if r.Hits <= 0 || r.Period <= 0 || r.StateHits < r.Hits {
			continue
		}
		if r.Period > maxWait {
			maxWait = r.Period
		}
	}
	if maxWait == 0 {
		return 0
	}
	return maxWait + rateLimitBuffer
}
