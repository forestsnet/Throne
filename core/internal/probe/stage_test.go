package probe

import (
	"context"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// A node that never takes the connection must be named as the node, not left
// behind a bare deadline that reads the same as a slow test target.
func TestUnreachableNodeIsNamedAsTheNode(t *testing.T) {
	dial := func(ctx context.Context, _, _ string) (net.Conn, error) {
		<-ctx.Done()
		return nil, ctx.Err()
	}
	client, closeClient := dialerHTTPClient(dial, 0)
	defer closeClient()

	_, err := urlTest(context.Background(), client, "https://example.invalid/generate_204", http.MethodGet, 200*time.Millisecond)
	if err == nil {
		t.Fatal("a dial that never returns must fail")
	}
	if !strings.Contains(err.Error(), "the node did not take the connection") {
		t.Fatalf("the failure does not name the node: %v", err)
	}
}

// The opposite case: the hop to the node works and the test target stays mute.
func TestSilentTargetIsNamedAsTheTarget(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	done := make(chan struct{})
	defer close(done)
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			// Takes the connection and answers nothing, like a node that carries
			// no traffic -- or a target that is simply slower than the budget.
			go func() {
				<-done
				_ = conn.Close()
			}()
		}
	}()

	dial := func(ctx context.Context, _, _ string) (net.Conn, error) {
		var dialer net.Dialer
		return dialer.DialContext(ctx, "tcp", listener.Addr().String())
	}
	client, closeClient := dialerHTTPClient(dial, 0)
	defer closeClient()

	_, err = urlTest(context.Background(), client, "http://probe.invalid/generate_204", http.MethodGet, 200*time.Millisecond)
	if err == nil {
		t.Fatal("a target that answers nothing must fail")
	}
	if !strings.Contains(err.Error(), "the node took the connection") || !strings.Contains(err.Error(), "probe.invalid") {
		t.Fatalf("the failure does not name the stage and the target: %v", err)
	}
	if strings.Contains(err.Error(), "TLS") {
		t.Fatalf("a plain http probe does no TLS, the failure must not claim one: %v", err)
	}
}

// The second request rides the connection the first one left open. When that
// connection goes quiet -- what a DPI box killing a live session looks like --
// the failure has to say so rather than blame the dial that already worked.
func TestReusedConnectionGoingSilentIsNamedAsSuch(t *testing.T) {
	var served atomic.Int64
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if served.Add(1) > 1 {
			// Answers nothing, and lets go as soon as the client gives up, so the
			// server's own shutdown is not left waiting on this handler.
			<-r.Context().Done()
			return
		}
		w.WriteHeader(http.StatusNoContent)
	}))
	defer server.Close()

	dial := func(ctx context.Context, _, _ string) (net.Conn, error) {
		var dialer net.Dialer
		return dialer.DialContext(ctx, "tcp", strings.TrimPrefix(server.URL, "http://"))
	}
	client, closeClient := dialerHTTPClient(dial, 0)
	defer closeClient()

	if _, err := urlTest(context.Background(), client, server.URL+"/generate_204", http.MethodGet, 2*time.Second); err != nil {
		t.Fatalf("the first request was answered, it must not fail: %v", err)
	}
	_, err := urlTest(context.Background(), client, server.URL+"/generate_204", http.MethodGet, 300*time.Millisecond)
	if err == nil {
		t.Fatal("the second request was never answered, it must fail")
	}
	if !strings.Contains(err.Error(), "went silent") {
		t.Fatalf("the failure does not say the open connection went quiet: %v", err)
	}
}
