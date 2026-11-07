package boxbox_test

import (
	"context"
	stdjson "encoding/json"
	"fmt"
	"net"
	"net/netip"
	"os"
	"testing"

	mDNS "github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	boxdns "github.com/sagernet/sing-box/dns"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing-box/route"
	"github.com/sagernet/sing/common/json"
	M "github.com/sagernet/sing/common/metadata"
	"github.com/sagernet/sing/service"
)

// Stubs replace only external I/O. All route and DNS rules come from the fixture.
type fixtureOutbound struct{ adapter.Outbound }

func (fixtureOutbound) Network() []string { return []string{"tcp", "udp"} }

type fixtureOutbounds struct{ adapter.OutboundManager }

func (fixtureOutbounds) Default() adapter.Outbound                { return fixtureOutbound{} }
func (fixtureOutbounds) Outbound(string) (adapter.Outbound, bool) { return fixtureOutbound{}, true }

type fixtureTransport struct {
	adapter.DNSTransport
	tag   string
	calls int
}

func (t *fixtureTransport) Type() string { return "udp" }
func (t *fixtureTransport) Tag() string  { return t.tag }
func (t *fixtureTransport) Exchange(ctx context.Context, msg *mDNS.Msg) (*mDNS.Msg, error) {
	t.calls++
	r := new(mDNS.Msg)
	r.SetReply(msg)
	q := msg.Question[0]
	h := mDNS.RR_Header{Name: q.Name, Rrtype: q.Qtype, Class: mDNS.ClassINET, Ttl: 60}
	if q.Qtype == mDNS.TypeA {
		r.Answer = []mDNS.RR{&mDNS.A{Hdr: h, A: net.ParseIP("192.0.2.10")}}
	}
	if q.Qtype == mDNS.TypeAAAA {
		r.Answer = []mDNS.RR{&mDNS.AAAA{Hdr: h, AAAA: net.ParseIP("2001:db8::10")}}
	}
	return r, nil
}
func (t *fixtureTransport) ExchangeAsync(ctx context.Context, msg *mDNS.Msg, cb func(*mDNS.Msg, error)) {
	r, e := t.Exchange(ctx, msg)
	cb(r, e)
}

type fixtureTransports struct {
	adapter.DNSTransportManager
	items map[string]*fixtureTransport
	final string
}

func (t *fixtureTransports) FakeIP() adapter.FakeIPTransport { return nil }
func (t *fixtureTransports) Transport(tag string) (adapter.DNSTransport, bool) {
	v, ok := t.items[tag]
	return v, ok
}
func (t *fixtureTransports) Default() adapter.DNSTransport { return t.items[t.final] }
func (t *fixtureTransports) Transports() []adapter.DNSTransport {
	var out []adapter.DNSTransport
	for _, t := range t.items {
		out = append(out, t)
	}
	return out
}

func checkGeneratedTUNIPv6Fixture(t *testing.T, path string) error {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var wrapped struct {
		Singbox stdjson.RawMessage `json:"singbox"`
	}
	if err = stdjson.Unmarshal(data, &wrapped); err != nil {
		return err
	}
	if len(wrapped.Singbox) > 0 {
		data = wrapped.Singbox
	}
	var envelope struct {
		Route stdjson.RawMessage            `json:"route"`
		DNS   map[string]stdjson.RawMessage `json:"dns"`
	}
	if err = stdjson.Unmarshal(data, &envelope); err != nil {
		return err
	}
	var servers []struct {
		Tag string `json:"tag"`
	}
	if err = stdjson.Unmarshal(envelope.DNS["servers"], &servers); err != nil {
		return err
	}
	transports := &fixtureTransports{items: make(map[string]*fixtureTransport)}
	for _, s := range servers {
		transports.items[s.Tag] = &fixtureTransport{tag: s.Tag}
	}
	if final, ok := envelope.DNS["final"]; ok {
		if err = stdjson.Unmarshal(final, &transports.final); err != nil {
			return err
		}
	} else if len(servers) > 0 {
		transports.final = servers[0].Tag
	}
	if transports.items[transports.final] == nil {
		return fmt.Errorf("missing default DNS transport %q", transports.final)
	}
	delete(envelope.DNS, "servers")
	dnsJSON, err := stdjson.Marshal(envelope.DNS)
	if err != nil {
		return err
	}
	ctx := service.ContextWith[adapter.DNSTransportManager](context.Background(), transports)
	ctx = service.ContextWith[adapter.OutboundManager](ctx, fixtureOutbounds{})
	ctx = service.ContextWith[adapter.DNSRouter](ctx, dnsRouterStub{})
	factory, err := log.New(log.Options{Context: ctx, Options: option.LogOptions{Disabled: true}})
	if err != nil {
		return err
	}
	var ro option.RouteOptions
	if err = json.UnmarshalContext(ctx, envelope.Route, &ro); err != nil {
		return fmt.Errorf("route decode: %w", err)
	}
	var dnsOptions option.DNSOptions
	if err = json.UnmarshalContext(ctx, dnsJSON, &dnsOptions); err != nil {
		return fmt.Errorf("DNS decode: %w", err)
	}
	router := route.NewRouter(ctx, factory, ro, dnsOptions)
	ctx = service.ContextWith[adapter.Router](ctx, router)
	if err = router.Initialize(ro.Rules, ro.RuleSet); err != nil {
		return fmt.Errorf("route initialize: %w", err)
	}
	checks := []struct {
		name, inbound, network, destination string
		reject                              bool
	}{
		{"TUN IPv6 TCP", "tun-in", "tcp", "[2001:db8::1]:443", true},
		{"TUN IPv6 UDP", "tun-in", "udp", "[2001:db8::1]:443", true},
		{"TUN IPv6 UDP DNS", "tun-in", "udp", "[fdfe:dcba:9876::2]:53", false},
		{"TUN IPv6 TCP DNS", "tun-in", "tcp", "[fdfe:dcba:9876::2]:53", false},
		{"TUN IPv4 UDP DNS", "tun-in", "udp", "172.19.0.2:53", false},
		{"TUN IPv4 TCP DNS", "tun-in", "tcp", "172.19.0.2:53", false},
		{"TUN IPv4 TCP", "tun-in", "tcp", "192.0.2.1:443", false},
		{"mixed IPv6 TCP", "mixed-in", "tcp", "[2001:db8::1]:443", false},
	}
	failures := 0
	for _, c := range checks {
		metadata := adapter.InboundContext{Inbound: c.inbound, Network: c.network, Source: M.ParseSocksaddr("[fdfe:dcba:9876::1]:50000"), Destination: M.ParseSocksaddr(c.destination)}
		action := router.PreMatch(metadata, nil).Action
		passed := (c.reject && action == adapter.PreMatchReject) || (!c.reject && action != adapter.PreMatchReject && action != adapter.PreMatchDrop)
		t.Logf("%s: action=%d passed=%t", c.name, action, passed)
		if !passed {
			failures++
		}
	}
	for _, network := range []string{"tcp", "udp"} {
		burstPassed := true
		for i := 0; i < 80; i++ {
			metadata := adapter.InboundContext{Inbound: "tun-in", Network: network, Source: M.ParseSocksaddr("[fdfe:dcba:9876::1]:50000"), Destination: M.ParseSocksaddr("[2001:db8::1]:443")}
			if a := router.PreMatch(metadata, nil).Action; a != adapter.PreMatchReject {
				t.Logf("%s reject burst failed at %d: action=%d", network, i+1, a)
				failures++
				burstPassed = false
				break
			}
		}
		t.Logf("80 immediate IPv6 %s rejects: passed=%t", network, burstPassed)
	}
	dnsRouter, err := boxdns.NewRouter(ctx, factory, dnsOptions)
	if err != nil {
		return err
	}
	if err = dnsRouter.Initialize(dnsOptions.Rules); err != nil {
		return fmt.Errorf("DNS initialize: %w", err)
	}
	scope := adapter.NewScope(ctx, factory.NewLogger("test"))
	defer scope.Close()
	if err = dnsRouter.Start(adapter.StartStateStart, scope); err != nil {
		return fmt.Errorf("DNS start: %w", err)
	}
	// Warm the shared cache through mixed-in first. TUN suppression must still win.
	for _, c := range []struct {
		inbound string
		qtype   uint16
		want    int
	}{{"mixed-in", mDNS.TypeAAAA, 1}, {"tun-in", mDNS.TypeAAAA, 0}, {"dns-system", mDNS.TypeAAAA, 0}, {"tun-in", mDNS.TypeA, 1}, {"mixed-in", mDNS.TypeAAAA, 1}} {
		msg := new(mDNS.Msg)
		msg.SetQuestion("regression.example.", c.qtype)
		metadata := adapter.InboundContext{Inbound: c.inbound, Network: "udp"}
		response, e := dnsRouter.Exchange(adapter.WithContext(ctx, &metadata), msg, adapter.DNSQueryOptions{})
		passed := e == nil && response != nil && response.Rcode == mDNS.RcodeSuccess && len(response.Answer) == c.want
		rcode, answers := -1, 0
		if response != nil {
			rcode, answers = response.Rcode, len(response.Answer)
		}
		t.Logf("DNS %s %s: passed=%t error=%v rcode=%d answers=%d", c.inbound, mDNS.TypeToString[c.qtype], passed, e, rcode, answers)
		if !passed {
			failures++
		}
	}
	upstreamCalls := 0
	for _, transport := range transports.items {
		upstreamCalls += transport.calls
	}
	if upstreamCalls != 2 {
		return fmt.Errorf("expected two upstream exchanges (AAAA warmup and A), got %d", upstreamCalls)
	}
	if failures > 0 {
		return fmt.Errorf("%d regression checks failed", failures)
	}
	t.Log("DNS cache reused the warmed AAAA answer without exposing it to TUN")
	t.Log("All generated-config route and DNS checks passed")
	return nil
}

// TestGeneratedTUNIPv6Rules consumes the application's actual -dump-configs output.
// The fixture stays outside the repository because real profiles may contain credentials.
// From core: THRONE_TUN_IPV6_FIXTURE=/path/profile.tun.json go test ./internal/boxbox -run TestGeneratedTUNIPv6Rules -count=1 -v
func TestGeneratedTUNIPv6Rules(t *testing.T) {
	path := os.Getenv("THRONE_TUN_IPV6_FIXTURE")
	if path == "" {
		t.Skip("set THRONE_TUN_IPV6_FIXTURE to generated IPv6-disabled TUN config")
	}
	if err := checkGeneratedTUNIPv6Fixture(t, path); err != nil {
		t.Fatal(err)
	}
}

type dnsRouterStub struct{ adapter.DNSRouter }

func (dnsRouterStub) LookupReverseMapping(netip.Addr) (string, bool) { return "", false }
