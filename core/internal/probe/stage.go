package probe

import (
	"crypto/tls"
	"fmt"
	"net/http/httptrace"
	"net/url"
	"sync"
)

// How far a probe request got before it failed. "context deadline exceeded" on
// its own names no stage, so a node that is merely far away reads exactly like
// one that is down, and the user is told a working server is dead.
type requestStages struct {
	connected  bool // the outbound returned a connection: the node took our traffic
	reused     bool // that connection was already open, left by the cold request
	tlsStarted bool
	secured    bool // TLS with the test target completed, so the node does forward
	answered   bool // the target sent its first byte
}

// http.Transport drives these hooks itself, so they fire whatever dialer the
// outbound hands it -- unlike ConnectStart and ConnectDone, which belong to
// net.Dialer and never run on a proxied dial.
type stageRecorder struct {
	mu     sync.Mutex
	stages requestStages
}

func (r *stageRecorder) trace() *httptrace.ClientTrace {
	return &httptrace.ClientTrace{
		GotConn: func(info httptrace.GotConnInfo) {
			r.mu.Lock()
			defer r.mu.Unlock()
			r.stages.connected = true
			r.stages.reused = info.Reused
		},
		TLSHandshakeStart: func() {
			// The transport layers TLS over the connection the outbound gave it,
			// so reaching this hook already proves the dial through the node worked.
			r.mu.Lock()
			defer r.mu.Unlock()
			r.stages.connected = true
			r.stages.tlsStarted = true
		},
		TLSHandshakeDone: func(_ tls.ConnectionState, err error) {
			r.mu.Lock()
			defer r.mu.Unlock()
			r.stages.secured = err == nil
		},
		GotFirstResponseByte: func() {
			r.mu.Lock()
			defer r.mu.Unlock()
			r.stages.answered = true
		},
	}
}

// A dial the expired context left running can still report a stage after
// client.Do returned, so the read goes through the lock as well.
func (r *stageRecorder) snapshot() requestStages {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.stages
}

// Says which leg did not finish, in that order: the hop to the node, the hop
// from the node to the test target, then the answer. The underlying error is
// kept whole -- the GUI matches "test aborted" and "context canceled" in it.
func describeRequestFailure(err error, stages requestStages, target string) error {
	switch {
	case !stages.connected:
		return fmt.Errorf("the node did not take the connection: %w", err)
	case stages.reused && !stages.answered:
		return fmt.Errorf("the open connection to the node went silent before %s answered: %w", target, err)
	case stages.tlsStarted && !stages.secured:
		return fmt.Errorf("the node took the connection, TLS with %s did not finish: %w", target, err)
	case !stages.answered:
		return fmt.Errorf("the node took the connection, %s did not answer: %w", target, err)
	default:
		return fmt.Errorf("%s answered, the response did not finish: %w", target, err)
	}
}

func testTargetName(raw string) string {
	if parsed, err := url.Parse(raw); err == nil && parsed.Host != "" {
		return parsed.Host
	}
	return raw
}
