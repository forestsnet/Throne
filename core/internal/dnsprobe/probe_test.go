package dnsprobe

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"strings"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing/common/control"
	"github.com/sagernet/sing/service"
)

type testEgress struct {
	err         error
	socketErr   error
	calls       atomic.Int32
	lastNetwork atomic.Value
}

func (e *testEgress) Control() (control.Func, error) {
	if e.err != nil {
		return nil, e.err
	}
	return func(network, address string, conn syscall.RawConn) error {
		e.calls.Add(1)
		e.lastNetwork.Store(network)
		return e.socketErr
	}, nil
}

type testCertificates struct {
	adapter.CertificateStore
	pool *x509.CertPool
}

func (c testCertificates) Pool() *x509.CertPool { return c.pool }

func TestEncryptedTransportsVerifyCertificateAndUseEgress(t *testing.T) {
	for _, network := range []string{"tls", "https"} {
		t.Run(network, func(t *testing.T) {
			answer := func(q *dns.Msg) *dns.Msg {
				r := new(dns.Msg)
				r.SetReply(q)
				r.Answer = []dns.RR{&dns.A{Hdr: dns.RR_Header{Name: q.Question[0].Name, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: 60}, A: net.ParseIP("192.0.2.99")}}
				return r
			}
			httpServer := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.URL.Path != "/custom-dns" {
					http.NotFound(w, r)
					return
				}
				wire, err := io.ReadAll(r.Body)
				if err != nil {
					t.Error(err)
					return
				}
				q := new(dns.Msg)
				if err = q.Unpack(wire); err != nil {
					t.Error(err)
					return
				}
				payload, err := answer(q).Pack()
				if err != nil {
					t.Error(err)
					return
				}
				w.Header().Set("Content-Type", "application/dns-message")
				_, _ = w.Write(payload)
			}))
			t.Cleanup(httpServer.Close)
			addr := httpServer.Listener.Addr().String()
			if network == "tls" {
				ln, err := tls.Listen("tcp4", "127.0.0.1:0", httpServer.TLS.Clone())
				if err != nil {
					t.Fatal(err)
				}
				s := &dns.Server{Net: "tcp-tls", Listener: ln, Handler: dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) { _ = w.WriteMsg(answer(q)) })}
				ready := make(chan struct{})
				s.NotifyStartedFunc = func() { close(ready) }
				go func() { _ = s.ActivateAndServe() }()
				<-ready
				t.Cleanup(func() { _ = s.Shutdown() })
				addr = ln.Addr().String()
			}
			endpoint := netip.MustParseAddrPort(addr)
			target := Target{Address: network + "://" + addr, Type: network, Host: endpoint.Addr().String(), Port: endpoint.Port(), Path: "/custom-dns"}
			egress := &testEgress{}
			untrusted, err := Run(context.Background(), egress, []Target{target}, "probe.example", nil, time.Second, nil)
			if err != nil {
				t.Fatal(err)
			}
			if untrusted[0].OK || untrusted[0].Responded || untrusted[0].Error == "" {
				t.Fatalf("untrusted TLS accepted: %+v", untrusted)
			}
			pool := x509.NewCertPool()
			pool.AddCert(httpServer.Certificate())
			ctx := service.ContextWith[adapter.CertificateStore](context.Background(), testCertificates{pool: pool})
			results, err := Run(ctx, egress, []Target{target}, "probe.example", nil, time.Second, nil)
			if err != nil {
				t.Fatal(err)
			}
			if !results[0].Responded || !results[0].OK || results[0].Addresses[0] != "192.0.2.99" {
				t.Fatalf("trusted response failed: %+v", results)
			}
			if egress.calls.Load() < 2 {
				t.Fatal("encrypted DNS did not use egress control")
			}
		})
	}
}

func TestEveryTransportFailsClosedWhenSocketBindingFails(t *testing.T) {
	for _, typ := range []string{"udp", "tcp", "tls", "https", "quic", "h3"} {
		t.Run(typ, func(t *testing.T) {
			egress := &testEgress{socketErr: errors.New("physical bind refused")}
			results, err := Run(context.Background(), egress, []Target{{Type: typ, Host: "192.0.2.1"}}, "probe.example", nil, time.Second, nil)
			if err != nil {
				t.Fatal(err)
			}
			if results[0].Responded || results[0].OK || results[0].Error == "" || egress.calls.Load() == 0 {
				t.Fatalf("bind failure was bypassed: %+v calls=%d", results, egress.calls.Load())
			}
			want := "tcp4"
			if typ == "udp" || typ == "quic" || typ == "h3" {
				want = "udp4"
			}
			if got := egress.lastNetwork.Load(); got != want {
				t.Fatalf("%s used %v, want %s", typ, got, want)
			}
		})
	}
}

func TestSilentDNSRespectsDeadline(t *testing.T) {
	pc, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()
	endpoint := netip.MustParseAddrPort(pc.LocalAddr().String())
	begin := time.Now()
	results, err := Run(context.Background(), &testEgress{}, []Target{{Type: "udp", Host: endpoint.Addr().String(), Port: endpoint.Port()}}, "probe.example", nil, 50*time.Millisecond, nil)
	if err != nil {
		t.Fatal(err)
	}
	if results[0].Responded || results[0].OK || results[0].Error == "" {
		t.Fatalf("silent DNS accepted: %+v", results)
	}
	if time.Since(begin) > time.Second {
		t.Fatal("probe outlived its timeout")
	}
}
func (*testEgress) InterfaceFor(netip.Addr) *control.Interface { return nil }

func serveDNS(t *testing.T, network string, rcode int, answer bool) Target {
	t.Helper()
	return serveDNSHandler(t, network, dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
		r := new(dns.Msg)
		r.SetReply(q)
		r.Rcode = rcode
		if answer && q.Question[0].Qtype == dns.TypeA {
			r.Answer = []dns.RR{&dns.A{Hdr: dns.RR_Header{Name: q.Question[0].Name, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: 60}, A: net.ParseIP("192.0.2.123")}}
		}
		_ = w.WriteMsg(r)
	}))
}

func serveDNSHandler(t *testing.T, network string, handler dns.Handler) Target {
	t.Helper()
	s := &dns.Server{Net: network, Handler: handler}
	var addr net.Addr
	if network == "udp" {
		pc, err := net.ListenPacket("udp4", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		s.PacketConn = pc
		addr = pc.LocalAddr()
	} else {
		ln, err := net.Listen("tcp4", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		s.Listener = ln
		addr = ln.Addr()
	}
	ready := make(chan struct{})
	s.NotifyStartedFunc = func() { close(ready) }
	go func() { _ = s.ActivateAndServe() }()
	<-ready
	t.Cleanup(func() { _ = s.Shutdown() })
	ap := netip.MustParseAddrPort(addr.String())
	return Target{Address: network + "://" + addr.String(), Type: network, Host: ap.Addr().String(), Port: ap.Port()}
}

func TestMalformedDNSResponseIsNotAnAnswer(t *testing.T) {
	for _, field := range []string{"id", "class", "question"} {
		t.Run(field, func(t *testing.T) {
			target := serveDNSHandler(t, "udp", dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
				r := new(dns.Msg)
				r.SetReply(q)
				switch field {
				case "id":
					r.Id++
				case "class":
					r.Question[0].Qclass = dns.ClassCHAOS
				case "question":
					r.Question[0].Name = "unrelated.example."
				}
				_ = w.WriteMsg(r)
			}))
			results, err := Run(context.Background(), &testEgress{}, []Target{target}, "probe.example", nil, 100*time.Millisecond, nil)
			if err != nil {
				t.Fatal(err)
			}
			if results[0].Responded || results[0].OK {
				t.Fatalf("malformed reply counted as answered: %+v", results)
			}
		})
	}
}

func TestAAAAFallbackSharesTheCandidateDeadline(t *testing.T) {
	var questions atomic.Int32
	target := serveDNSHandler(t, "udp", dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
		questions.Add(1)
		if q.Question[0].Qtype == dns.TypeA {
			r := new(dns.Msg)
			r.SetReply(q)
			_ = w.WriteMsg(r)
		}
	}))
	begin := time.Now()
	results, err := Run(context.Background(), &testEgress{}, []Target{target}, "probe.example", nil, 80*time.Millisecond, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !results[0].Responded || results[0].OK || questions.Load() != 2 {
		t.Fatalf("wrong fallback: %+v questions=%d", results, questions.Load())
	}
	if elapsed := time.Since(begin); elapsed > 400*time.Millisecond {
		t.Fatalf("fallback exceeded shared deadline: %v", elapsed)
	}
}

func TestProbeClosesTCPConnectionBeforeReturning(t *testing.T) {
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	// The transport opens both an exchange socket and a reuse-probe socket.
	closed := make(chan error, 8)
	var accepted atomic.Int32
	go func() {
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			accepted.Add(1)
			go func() {
				defer conn.Close()
				_ = conn.SetDeadline(time.Now().Add(2 * time.Second))
				wire := &dns.Conn{Conn: conn}
				for {
					q, err := wire.ReadMsg()
					if err != nil {
						closed <- err
						return
					}
					r := new(dns.Msg)
					r.SetReply(q)
					r.Rcode = dns.RcodeNameError
					if err = wire.WriteMsg(r); err != nil {
						closed <- err
						return
					}
				}
			}()
		}
	}()
	endpoint := netip.MustParseAddrPort(ln.Addr().String())
	results, err := Run(context.Background(), &testEgress{}, []Target{{Type: "tcp", Host: endpoint.Addr().String(), Port: endpoint.Port()}}, "probe.example", nil, time.Second, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !results[0].Responded {
		t.Fatalf("no valid response: %+v", results)
	}
	if accepted.Load() == 0 {
		t.Fatal("no TCP connection accepted")
	}
	for range accepted.Load() {
		select {
		case err := <-closed:
			if !errors.Is(err, io.EOF) {
				t.Fatalf("socket not closed by probe scope: %v", err)
			}
		case <-time.After(time.Second):
			t.Fatal("probe left TCP connection open")
		}
	}
}

func TestDirectExchangeUsesEgressAndDistinguishesAnswers(t *testing.T) {
	for _, network := range []string{"udp", "tcp"} {
		for _, tc := range []struct {
			name       string
			rcode      int
			answer, ok bool
		}{{"answer", dns.RcodeSuccess, true, true}, {"nxdomain", dns.RcodeNameError, false, false}, {"nodata", dns.RcodeSuccess, false, false}} {
			t.Run(network+"/"+tc.name, func(t *testing.T) {
				target := serveDNS(t, network, tc.rcode, tc.answer)
				egress := &testEgress{}
				results, err := Run(context.Background(), egress, []Target{target}, "probe.example", nil, time.Second, nil)
				if err != nil {
					t.Fatal(err)
				}
				if len(results) != 1 || !results[0].Responded || results[0].OK != tc.ok {
					t.Fatalf("unexpected result: %+v", results)
				}
				if tc.ok && (len(results[0].Addresses) != 1 || results[0].Addresses[0] != "192.0.2.123") {
					t.Fatalf("wrong addresses: %+v", results[0])
				}
				if egress.calls.Load() == 0 {
					t.Fatal("DNS socket bypassed the egress control")
				}
			})
		}
	}
}

func TestNoEgressRefusesBatch(t *testing.T) {
	want := errors.New("egress unavailable")
	_, err := Run(context.Background(), &testEgress{err: want}, []Target{{Type: "udp", Host: "192.0.2.1", Port: 53}}, "probe.example", nil, time.Second, nil)
	if !errors.Is(err, want) {
		t.Fatalf("got %v, want %v", err, want)
	}
}

func TestHostnameWithoutPhysicalBootstrapFailsClosed(t *testing.T) {
	egress := &testEgress{}
	results, err := Run(context.Background(), egress, []Target{{Address: "tls://dns.example", Type: "tls", Host: "dns.example", Port: 853}}, "probe.example", nil, time.Second, nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(results) != 1 || results[0].Responded || results[0].OK || results[0].Error == "" {
		t.Fatalf("unexpected result: %+v", results)
	}
	if egress.calls.Load() != 0 {
		t.Fatal("attempted a socket without direct bootstrap")
	}
}

func TestUnsupportedLocalIsNotSuccessful(t *testing.T) {
	results, err := Run(context.Background(), &testEgress{}, []Target{{Address: "local", Type: "local"}, {Address: "dhcp://auto", Type: "dhcp"}}, "probe.example", nil, time.Second, nil)
	if err != nil {
		t.Fatal(err)
	}
	for _, r := range results {
		if r.OK || r.Responded || r.Error == "" {
			t.Fatalf("unexpected result: %+v", r)
		}
	}
}

// Остальные имена конфига разрешаются тем же, уже поднятым транспортом, и
// неудача одного имени не отменяет остальные: закрепить надо то, что ответило.
func TestExtraNamesResolveThroughTheSameTransport(t *testing.T) {
	known := map[string]string{
		"probe.example": "192.0.2.1",
		"first.example": "192.0.2.2",
		"third.example": "192.0.2.4",
	}
	target := serveDNSHandler(t, "udp", dns.HandlerFunc(func(w dns.ResponseWriter, q *dns.Msg) {
		r := new(dns.Msg)
		r.SetReply(q)
		address, found := known[strings.TrimSuffix(q.Question[0].Name, ".")]
		switch {
		case !found:
			r.Rcode = dns.RcodeNameError
		case q.Question[0].Qtype == dns.TypeA:
			r.Answer = []dns.RR{&dns.A{Hdr: dns.RR_Header{Name: q.Question[0].Name, Rrtype: dns.TypeA, Class: dns.ClassINET, Ttl: 60}, A: net.ParseIP(address)}}
		}
		_ = w.WriteMsg(r)
	}))
	extra := []string{"first.example", "second.example", "third.example"}
	results, err := Run(context.Background(), &testEgress{}, []Target{target}, "probe.example", extra, time.Second, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !results[0].OK || len(results[0].Addresses) != 1 || results[0].Addresses[0] != "192.0.2.1" {
		t.Fatalf("проверяемое имя: %+v", results[0])
	}
	if len(results[0].Extra) != len(extra) {
		t.Fatalf("остальных имён в ответе %d, спрашивали %d", len(results[0].Extra), len(extra))
	}
	for i, want := range []struct{ name, address string }{
		{"first.example", "192.0.2.2"},
		{"second.example", ""},
		{"third.example", "192.0.2.4"},
	} {
		got := results[0].Extra[i]
		if got.Name != want.name {
			t.Fatalf("порядок не сохранён: на месте %s стоит %s", want.name, got.Name)
		}
		if want.address == "" {
			if got.Error == "" || len(got.Addresses) != 0 {
				t.Fatalf("%s должно было не разрешиться: %+v", want.name, got)
			}
			continue
		}
		if len(got.Addresses) != 1 || got.Addresses[0] != want.address {
			t.Fatalf("%s: %+v", want.name, got)
		}
	}
}

// Круг по всем кандидатам с полным списком имён — это десятки запросов в
// секунду с одного адреса, и отказать на это резолвер вправе. Второй проход
// идёт по одной цели, и держится это проверкой, а не обычаем.
func TestExtraNamesRequireASingleTarget(t *testing.T) {
	targets := []Target{serveDNS(t, "udp", dns.RcodeSuccess, true), serveDNS(t, "udp", dns.RcodeSuccess, true)}
	_, err := Run(context.Background(), &testEgress{}, targets, "probe.example", []string{"first.example"}, time.Second, nil)
	if err == nil {
		t.Fatal("ждали отказа на список имён при двух целях")
	}
}

// Имена приходят из конфига подписки, то есть снаружи. Негодное имя должно
// отбиваться до того, как уйдёт в сеть.
func TestExtraNamesAreValidated(t *testing.T) {
	target := serveDNS(t, "udp", dns.RcodeSuccess, true)
	for _, bad := range []string{"", "   ", "a..b", strings.Repeat("a", 300)} {
		_, err := Run(context.Background(), &testEgress{}, []Target{target}, "probe.example", []string{bad}, time.Second, nil)
		if err == nil {
			t.Fatalf("приняли негодное имя %q", bad)
		}
	}
}
