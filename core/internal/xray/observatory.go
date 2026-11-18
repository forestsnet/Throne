package xray

import (
	"context"
	"sync"
	"time"

	"github.com/xtls/xray-core/app/observatory"
	"github.com/xtls/xray-core/core"
	"github.com/xtls/xray-core/features/extension"
)

// What a config whose routing leans on an observatory is given to rate its
// first node. Such a config does not merely answer slowly before its first
// observation: a balancer picking by leastPing returns an empty tag while
// nothing is rated, the route resolves to nowhere and the request fails at
// once, so no request budget helps and only waiting for the observation does.
// The plain observer probes its subjects one at a time and sleeps
// probeInterval, 10 s by default, between them, so this covers the first result
// and one node after it rather than a whole round.
const ObservationTimeout = 20 * time.Second

const observationPoll = 100 * time.Millisecond

// Present only when the config carries an observatory or a burstObservatory.
func ObservatoryOf(instance *core.Instance) extension.Observatory {
	if instance == nil {
		return nil
	}
	observer, _ := instance.GetFeature(extension.ObservatoryType()).(extension.Observatory)
	return observer
}

// Reports whether the observatory rated a subject alive before the wait ran
// out. The caller carries on either way: a config whose nodes are all genuinely
// dead has to come out of the probe as dead, not as a failure of this wait.
// An instance without an observatory is ready as it stands.
func AwaitObservation(ctx context.Context, instance *core.Instance, timeout time.Duration) bool {
	return awaitObserver(ctx, ObservatoryOf(instance), timeout)
}

func awaitObserver(ctx context.Context, observer extension.Observatory, timeout time.Duration) bool {
	if observer == nil {
		return true
	}
	deadline := time.Now().Add(timeout)
	for {
		if observedAlive(ctx, observer) {
			return true
		}
		if !time.Now().Before(deadline) {
			return false
		}
		select {
		case <-ctx.Done():
			return false
		case <-time.After(observationPoll):
		}
	}
}

func observedAlive(ctx context.Context, observer extension.Observatory) bool {
	report, err := observer.GetObservation(ctx)
	if err != nil {
		return false
	}
	result, ok := report.(*observatory.ObservationResult)
	if !ok {
		return false
	}
	for _, status := range result.GetStatus() {
		if status.GetAlive() {
			return true
		}
	}
	return false
}

// Waits for every instance that has an observatory at the same time, so the
// cost is the slowest of them and not their sum, and nothing at all when no
// config carries one. Call it once the box is up: the observatory's own probes
// resolve names through it. Returns the indexes that rated nothing in time.
func AwaitObservations(ctx context.Context, instances []*core.Instance, timeout time.Duration) []int {
	var wg sync.WaitGroup
	timedOut := make([]bool, len(instances))
	for idx, instance := range instances {
		if ObservatoryOf(instance) == nil {
			continue
		}
		wg.Add(1)
		go func() {
			defer wg.Done()
			timedOut[idx] = !AwaitObservation(ctx, instance, timeout)
		}()
	}
	wg.Wait()
	var pending []int
	for idx, missed := range timedOut {
		if missed {
			pending = append(pending, idx)
		}
	}
	return pending
}
