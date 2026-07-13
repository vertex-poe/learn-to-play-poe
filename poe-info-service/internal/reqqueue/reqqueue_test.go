package reqqueue

import (
	"context"
	"net/http"
	"strconv"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// testHeaderParser reads a minimal made-up rate-limit header shape for
// tests, standing in for either real API's actual header scheme (which one
// it is doesn't matter to the queue — see HeaderParser's doc comment).
func testHeaderParser(h http.Header) (string, []Rule, bool) {
	key := h.Get("X-Test-Policy")
	if key == "" {
		return "", nil, false
	}
	hits, _ := strconv.Atoi(h.Get("X-Test-Hits"))
	periodMs, _ := strconv.Atoi(h.Get("X-Test-Period-Ms"))
	state, _ := strconv.Atoi(h.Get("X-Test-State"))
	return key, []Rule{{Name: "r", Hits: hits, Period: time.Duration(periodMs) * time.Millisecond, StateHits: state}}, true
}

func rateHeaders(policy string, hits, periodMs, state int) http.Header {
	h := http.Header{}
	h.Set("X-Test-Policy", policy)
	h.Set("X-Test-Hits", strconv.Itoa(hits))
	h.Set("X-Test-Period-Ms", strconv.Itoa(periodMs))
	h.Set("X-Test-State", strconv.Itoa(state))
	return h
}

func newTestQueue(t *testing.T) (*Queue, context.Context) {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	return New(ctx, testHeaderParser), ctx
}

func TestComputeDelay(t *testing.T) {
	cases := []struct {
		name  string
		rules []Rule
		want  time.Duration
	}{
		{"no rules", nil, 0},
		{"headroom remains", []Rule{{Hits: 10, Period: time.Second, StateHits: 3}}, 0},
		{"exactly saturated", []Rule{{Hits: 10, Period: 200 * time.Millisecond, StateHits: 10}}, 200*time.Millisecond + rateLimitBuffer},
		{"over saturated still gates", []Rule{{Hits: 10, Period: 200 * time.Millisecond, StateHits: 12}}, 200*time.Millisecond + rateLimitBuffer},
		{
			"multiple rules, most restrictive wins",
			[]Rule{
				{Hits: 10, Period: 100 * time.Millisecond, StateHits: 10}, // saturated, fast
				{Hits: 30, Period: 500 * time.Millisecond, StateHits: 30}, // saturated, slow
				{Hits: 5, Period: 50 * time.Millisecond, StateHits: 1},    // headroom, ignored
			},
			500*time.Millisecond + rateLimitBuffer,
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := computeDelay(tc.rules)
			if got != tc.want {
				t.Errorf("computeDelay(%+v) = %v, want %v", tc.rules, got, tc.want)
			}
		})
	}
}

// TestQueue_Submit_DedupMergesPriorityAndWaiter proves re-submitting a Key
// still queued/in-flight collapses onto the single existing task — one
// actual Exec call, promoted to the higher of the two requested priorities,
// and both Waiters resolve to the same result.
func TestQueue_Submit_DedupMergesPriorityAndWaiter(t *testing.T) {
	q, _ := newTestQueue(t)

	var execCount atomic.Int32
	release := make(chan struct{})
	exec := func(ctx context.Context) (any, http.Header, error) {
		execCount.Add(1)
		<-release
		return "result", nil, nil
	}

	w1 := q.Submit(Task{Key: "k", Priority: PriorityLow, Exec: exec})
	// Give the dispatch loop a moment to pick up w1 before the second
	// Submit, so both land on the same in-flight entry rather than racing
	// a Submit against dispatch's delete-from-byKey-on-completion.
	time.Sleep(20 * time.Millisecond)
	w2 := q.Submit(Task{Key: "k", Priority: PriorityHigh, Exec: exec})

	q.mu.Lock()
	gotPriority := q.byKey["k"].task.Priority
	q.mu.Unlock()
	if gotPriority != PriorityHigh {
		t.Errorf("merged priority = %d, want %d (max of the two Submits)", gotPriority, PriorityHigh)
	}

	close(release)

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	r1, err1 := w1.Wait(ctx)
	r2, err2 := w2.Wait(ctx)
	if err1 != nil || err2 != nil {
		t.Fatalf("Wait errors: %v, %v", err1, err2)
	}
	if r1 != "result" || r2 != "result" {
		t.Errorf("both waiters should see the single execution's result, got %v / %v", r1, r2)
	}
	if n := execCount.Load(); n != 1 {
		t.Errorf("Exec ran %d times, want exactly 1", n)
	}
}

// TestQueue_Cancel_RemovesQueuedTask_NoopIfUnknown proves Cancel drops a
// not-yet-dispatched task (its Waiter then sees ErrCancelled) and is a
// harmless no-op for a key that isn't currently queued or in flight.
func TestQueue_Cancel_RemovesQueuedTask_NoopIfUnknown(t *testing.T) {
	q, _ := newTestQueue(t)

	// Seed a long gate on policy "gate" so a second same-policy task is
	// guaranteed to still be sitting in q.pending (not yet dispatched) when
	// we Cancel it, rather than racing the dispatch loop.
	gateDone := make(chan struct{})
	q.Submit(Task{
		Key: "seed", PolicyHint: "gate",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			close(gateDone)
			return nil, rateHeaders("gate", 1, 5000, 1), nil // saturated for ~5s
		},
	})
	<-gateDone
	time.Sleep(20 * time.Millisecond) // let dispatch's policy update land

	w := q.Submit(Task{Key: "queued", PolicyHint: "gate", Exec: func(ctx context.Context) (any, http.Header, error) {
		t.Error("Exec should never run for a cancelled task")
		return nil, nil, nil
	}})

	q.Cancel("queued")

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if _, err := w.Wait(ctx); err != ErrCancelled {
		t.Errorf("Wait after Cancel = %v, want ErrCancelled", err)
	}

	// Cancelling an unknown/already-completed key must not panic or block.
	q.Cancel("queued") // already gone
	q.Cancel("no-such-key")
}

// TestQueue_SetPriority_UpdatesQueuedEntry proves SetPriority mutates an
// already-queued task's effective priority, and is a no-op for an unknown
// key.
func TestQueue_SetPriority_UpdatesQueuedEntry(t *testing.T) {
	q, _ := newTestQueue(t)

	gateDone := make(chan struct{})
	q.Submit(Task{
		Key: "seed", PolicyHint: "gate2",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			close(gateDone)
			return nil, rateHeaders("gate2", 1, 5000, 1), nil
		},
	})
	<-gateDone
	time.Sleep(20 * time.Millisecond)

	q.Submit(Task{Key: "queued", Priority: PriorityLow, PolicyHint: "gate2", Exec: func(ctx context.Context) (any, http.Header, error) {
		return "done", nil, nil
	}})

	q.SetPriority("queued", PriorityImmediate)
	q.mu.Lock()
	got := q.byKey["queued"].task.Priority
	q.mu.Unlock()
	if got != PriorityImmediate {
		t.Errorf("priority after SetPriority = %d, want %d", got, PriorityImmediate)
	}

	q.SetPriority("no-such-key", PriorityImmediate) // must not panic
}

// TestQueue_PolicyGating_DelaysSamePolicyNotOthers proves a saturated
// policy delays the next task sharing its key, while a task under a
// different (or no) policy is unaffected.
func TestQueue_PolicyGating_DelaysSamePolicyNotOthers(t *testing.T) {
	q, _ := newTestQueue(t)

	seedDone := make(chan struct{})
	q.Submit(Task{
		Key: "seed", PolicyHint: "p",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			close(seedDone)
			return nil, rateHeaders("p", 1, 50, 1), nil // saturated, 50ms period (+1s buffer)
		},
	})
	<-seedDone
	time.Sleep(20 * time.Millisecond) // let dispatch's policy update land

	start := time.Now()
	var gatedElapsed, freeElapsed time.Duration
	var wg sync.WaitGroup
	wg.Add(2)

	q.Submit(Task{Key: "gated", PolicyHint: "p", Exec: func(ctx context.Context) (any, http.Header, error) {
		gatedElapsed = time.Since(start)
		wg.Done()
		return nil, nil, nil
	}})
	q.Submit(Task{Key: "free", Exec: func(ctx context.Context) (any, http.Header, error) {
		freeElapsed = time.Since(start)
		wg.Done()
		return nil, nil, nil
	}})

	done := make(chan struct{})
	go func() { wg.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("tasks never completed")
	}

	if freeElapsed > 200*time.Millisecond {
		t.Errorf("ungated task took %v, want well under the gated policy's delay", freeElapsed)
	}
	if gatedElapsed < 900*time.Millisecond {
		t.Errorf("gated task ran after only %v, want it held back by ~1s (period + rateLimitBuffer)", gatedElapsed)
	}
}

// TestQueue_PriorityOrder_HighestDispatchesFirstUnderContention proves that
// once a shared policy's gate clears, the higher-priority of two tasks that
// queued up behind it dispatches first, regardless of submission order.
func TestQueue_PriorityOrder_HighestDispatchesFirstUnderContention(t *testing.T) {
	q, _ := newTestQueue(t)

	seedDone := make(chan struct{})
	q.Submit(Task{
		Key: "seed", PolicyHint: "p",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			close(seedDone)
			return nil, rateHeaders("p", 1, 50, 1), nil
		},
	})
	<-seedDone
	time.Sleep(20 * time.Millisecond)

	var mu sync.Mutex
	var order []string
	record := func(name string) func(ctx context.Context) (any, http.Header, error) {
		return func(ctx context.Context) (any, http.Header, error) {
			mu.Lock()
			order = append(order, name)
			mu.Unlock()
			return nil, nil, nil
		}
	}

	// Submitted low-priority first, high-priority second — dispatch order
	// should still be high-then-low.
	wLow := q.Submit(Task{Key: "low", Priority: PriorityLow, PolicyHint: "p", Exec: record("low")})
	wHigh := q.Submit(Task{Key: "high", Priority: PriorityHigh, PolicyHint: "p", Exec: record("high")})

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := wLow.Wait(ctx); err != nil {
		t.Fatalf("low Wait: %v", err)
	}
	if _, err := wHigh.Wait(ctx); err != nil {
		t.Fatalf("high Wait: %v", err)
	}

	mu.Lock()
	defer mu.Unlock()
	if len(order) != 2 || order[0] != "high" || order[1] != "low" {
		t.Errorf("dispatch order = %v, want [high low]", order)
	}
}

// TestQueue_HintPolicyAlias_GatesRepeatedCallsUnderSameHint proves that once
// a task's response reveals its real rate-limit policy name (which need not
// match the caller's PolicyHint at all — the common case, since a hint is
// just a caller-chosen grouping label), a later task submitted with that
// same hint is correctly paced by the learned policy, not left ungated
// forever just because the hint string itself was never a real policy key.
func TestQueue_HintPolicyAlias_GatesRepeatedCallsUnderSameHint(t *testing.T) {
	q, _ := newTestQueue(t)

	firstDone := make(chan struct{})
	q.Submit(Task{
		Key: "call-1", PolicyHint: "endpoint-x",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			defer close(firstDone)
			// The real policy name the server reports has nothing to do
			// with the "endpoint-x" hint string.
			return nil, rateHeaders("some-real-policy-name", 1, 50, 1), nil
		},
	})
	<-firstDone
	time.Sleep(20 * time.Millisecond) // let dispatch's policy/hint-alias update land

	start := time.Now()
	w := q.Submit(Task{Key: "call-2", PolicyHint: "endpoint-x", Exec: func(ctx context.Context) (any, http.Header, error) {
		return nil, nil, nil
	}})

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if _, err := w.Wait(ctx); err != nil {
		t.Fatalf("Wait: %v", err)
	}
	elapsed := time.Since(start)
	if elapsed < 900*time.Millisecond {
		t.Errorf("second call under the same hint ran after only %v, want it gated by ~1s (the learned policy's period + rateLimitBuffer)", elapsed)
	}
}

// TestQueue_Policies_ReportsLearnedStateSortedByKey proves Policies()
// surfaces every policy this queue has actually dispatched a task under, in
// sorted order, and that a hint never dispatched under is simply absent.
func TestQueue_Policies_ReportsLearnedStateSortedByKey(t *testing.T) {
	q, _ := newTestQueue(t)

	if got := q.Policies(); len(got) != 0 {
		t.Fatalf("Policies() on a fresh queue = %v, want empty", got)
	}

	done := make(chan struct{})
	q.Submit(Task{
		Key: "call-1", PolicyHint: "zebra-hint",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			defer close(done)
			return nil, rateHeaders("zebra-policy", 10, 1000, 3), nil
		},
	})
	<-done
	time.Sleep(20 * time.Millisecond) // let dispatch's policy update land

	done2 := make(chan struct{})
	q.Submit(Task{
		Key: "call-2", PolicyHint: "apple-hint",
		Exec: func(ctx context.Context) (any, http.Header, error) {
			defer close(done2)
			return nil, rateHeaders("apple-policy", 5, 500, 5), nil
		},
	})
	<-done2
	time.Sleep(20 * time.Millisecond)

	got := q.Policies()
	if len(got) != 2 {
		t.Fatalf("Policies() = %+v, want 2 entries", got)
	}
	// Sorted by Policy key: "apple-policy" before "zebra-policy".
	if got[0].Policy != "apple-policy" || got[1].Policy != "zebra-policy" {
		t.Errorf("Policies() order = [%s %s], want [apple-policy zebra-policy]", got[0].Policy, got[1].Policy)
	}
	if len(got[0].Rules) != 1 || got[0].Rules[0].Hits != 5 || got[0].Rules[0].StateHits != 5 {
		t.Errorf("apple-policy rules = %+v, want Hits=5 StateHits=5", got[0].Rules)
	}
	// apple-policy was saturated (StateHits==Hits==5), so it should be
	// gated until some future NextAllowedAt.
	if !got[0].NextAllowedAt.After(time.Now()) {
		t.Errorf("apple-policy NextAllowedAt = %v, want it in the future (saturated)", got[0].NextAllowedAt)
	}
	// zebra-policy had headroom (StateHits 3 < Hits 10), so it's clear now.
	if got[1].NextAllowedAt.After(time.Now()) {
		t.Errorf("zebra-policy NextAllowedAt = %v, want it clear (not saturated)", got[1].NextAllowedAt)
	}
}

// TestWaiter_Wait_ContextTimeout_TaskStillCompletesInBackground proves a
// Wait call abandoned by a timed-out/cancelled ctx doesn't abort the
// underlying task — it keeps running and a later Waiter on the same
// still-in-flight Key still observes its result.
func TestWaiter_Wait_ContextTimeout_TaskStillCompletesInBackground(t *testing.T) {
	q, _ := newTestQueue(t)

	release := make(chan struct{})
	w1 := q.Submit(Task{Key: "slow", Exec: func(ctx context.Context) (any, http.Header, error) {
		<-release
		return "eventual result", nil, nil
	}})

	shortCtx, cancel := context.WithTimeout(context.Background(), 30*time.Millisecond)
	defer cancel()
	if _, err := w1.Wait(shortCtx); err != context.DeadlineExceeded {
		t.Fatalf("Wait with an expiring ctx = %v, want context.DeadlineExceeded", err)
	}

	// The task is still in flight — a second Submit on the same Key must
	// merge onto it rather than starting a duplicate.
	w2 := q.Submit(Task{Key: "slow", Exec: func(ctx context.Context) (any, http.Header, error) {
		t.Error("Exec should not run a second time for an in-flight Key")
		return nil, nil, nil
	}})

	close(release)
	longCtx, cancel2 := context.WithTimeout(context.Background(), time.Second)
	defer cancel2()
	result, err := w2.Wait(longCtx)
	if err != nil {
		t.Fatalf("second waiter Wait: %v", err)
	}
	if result != "eventual result" {
		t.Errorf("second waiter result = %v, want the original task's result", result)
	}
}
