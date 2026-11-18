package xray

import (
	"context"
	"testing"
	"time"

	"github.com/xtls/xray-core/app/observatory"
	"github.com/xtls/xray-core/core"
	"github.com/xtls/xray-core/features/extension"
	"google.golang.org/protobuf/proto"
)

type fakeObserver struct {
	status []*observatory.OutboundStatus
	calls  int
}

func (f *fakeObserver) Type() interface{} { return extension.ObservatoryType() }
func (f *fakeObserver) Start() error      { return nil }
func (f *fakeObserver) Close() error      { return nil }

func (f *fakeObserver) GetObservation(context.Context) (proto.Message, error) {
	f.calls++
	return &observatory.ObservationResult{Status: f.status}, nil
}

// A config with no observatory must not pay for one.
func TestNoObservatoryIsReadyAtOnce(t *testing.T) {
	begin := time.Now()
	if !awaitObserver(context.Background(), nil, time.Hour) {
		t.Fatal("an instance without an observatory is ready as it stands")
	}
	if waited := time.Since(begin); waited > time.Second {
		t.Fatalf("the wait took %s, it must not happen at all", waited)
	}
}

func TestARatedNodeEndsTheWait(t *testing.T) {
	observer := &fakeObserver{status: []*observatory.OutboundStatus{
		{OutboundTag: "dead", Alive: false, Delay: 0},
		{OutboundTag: "live", Alive: true, Delay: 180},
	}}
	begin := time.Now()
	if !awaitObserver(context.Background(), observer, time.Hour) {
		t.Fatal("one rated node is enough for a balancer to pick")
	}
	if waited := time.Since(begin); waited > time.Second {
		t.Fatalf("the wait took %s with a node already rated", waited)
	}
}

// All dead is a verdict, not a reason to hold the test back forever.
func TestNothingRatedGivesUpOnTime(t *testing.T) {
	observer := &fakeObserver{status: []*observatory.OutboundStatus{{OutboundTag: "dead", Alive: false}}}
	begin := time.Now()
	if awaitObserver(context.Background(), observer, 250*time.Millisecond) {
		t.Fatal("no node was rated alive, the wait must report that")
	}
	waited := time.Since(begin)
	if waited < 250*time.Millisecond {
		t.Fatalf("gave up after %s, before the wait it was given", waited)
	}
	if waited > 5*time.Second {
		t.Fatalf("held on for %s past its own deadline", waited)
	}
	if observer.calls < 2 {
		t.Fatalf("asked the observatory %d time(s), it must keep asking while it waits", observer.calls)
	}
}

// Pressing Stop must not leave the user sitting out the whole wait.
func TestCancelEndsTheWait(t *testing.T) {
	observer := &fakeObserver{}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	begin := time.Now()
	if awaitObserver(ctx, observer, time.Hour) {
		t.Fatal("a cancelled wait rated nothing")
	}
	if waited := time.Since(begin); waited > time.Second {
		t.Fatalf("a cancelled wait took %s", waited)
	}
}

const outboundOnly = `{"outbounds":[{"protocol":"freedom","tag":"direct"}]}`

const withObservatory = `{"outbounds":[{"protocol":"freedom","tag":"direct"}],
	"observatory":{"subjectSelector":["direct"],"probeInterval":"10s"}}`

const withBurstObservatory = `{"outbounds":[{"protocol":"freedom","tag":"direct"}],
	"burstObservatory":{"subjectSelector":["direct"],"pingConfig":{}}}`

// The one thing a fake cannot answer: whether this Xray still hands the
// observatory out under the type we ask for. Nothing is started, so no probe
// leaves the machine.
func TestObservatoryIsFoundOnlyWhenTheConfigHasOne(t *testing.T) {
	for _, item := range []struct {
		name   string
		config string
		want   bool
	}{
		{"outbound only", outboundOnly, false},
		{"observatory", withObservatory, true},
		{"burst observatory", withBurstObservatory, true},
	} {
		t.Run(item.name, func(t *testing.T) {
			instance, err := CreateXrayInstance(item.config)
			if err != nil {
				t.Fatalf("the config did not build: %v", err)
			}
			t.Cleanup(func() { _ = instance.Close() })
			if got := ObservatoryOf(instance) != nil; got != item.want {
				t.Fatalf("observatory found: %v, expected %v", got, item.want)
			}
		})
	}
}

func TestOnlyConfigsWithAnObservatoryAreWaitedFor(t *testing.T) {
	var instances []*core.Instance
	for _, config := range []string{outboundOnly, withObservatory, outboundOnly} {
		instance, err := CreateXrayInstance(config)
		if err != nil {
			t.Fatalf("the config did not build: %v", err)
		}
		t.Cleanup(func() { _ = instance.Close() })
		instances = append(instances, instance)
	}
	begin := time.Now()
	pending := AwaitObservations(context.Background(), instances, 200*time.Millisecond)
	waited := time.Since(begin)
	if len(pending) != 1 || pending[0] != 1 {
		t.Fatalf("waited for %v, only the config at index 1 carries an observatory", pending)
	}
	if waited < 200*time.Millisecond {
		t.Fatalf("returned after %s, before the wait the one observatory was given", waited)
	}
	if waited > 2*time.Second {
		t.Fatalf("took %s, the two configs without an observatory must cost nothing", waited)
	}
}

// Nil as a whole list, and a nil instance inside one, both turn up on the
// paths that build fewer instances than tags.
func TestNoInstancesIsNoWait(t *testing.T) {
	if pending := AwaitObservations(context.Background(), nil, time.Hour); pending != nil {
		t.Fatalf("an empty list waited for %v", pending)
	}
	if pending := AwaitObservations(context.Background(), []*core.Instance{nil}, time.Hour); pending != nil {
		t.Fatalf("a nil instance waited for %v", pending)
	}
}
