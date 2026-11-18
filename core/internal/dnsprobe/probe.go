// Package dnsprobe checks DNS servers through a caller-supplied physical egress.
// It never uses a running DNS router, its cache, or the OS name resolver.
package dnsprobe

import (
	"context"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"strings"
	"sync"
	"syscall"
	"time"

	"ThroneCore/internal/scan"
	"github.com/miekg/dns"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/dns/transport"
	quictransport "github.com/sagernet/sing-box/dns/transport/quic"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
	"github.com/sagernet/sing/service"
)

const directTag = "dns-probe-physical"

type Target struct {
	Address string
	Type    string
	Host    string
	Port    uint16
	Path    string
}

type Result struct {
	Address   string
	Responded bool
	OK        bool
	LatencyMS int32
	Error     string
	Addresses []string
	// Остальные имена конфига, в том порядке, в каком их спросили.
	Extra []NameResult
}

// Одно имя и то, чем по нему ответили. Неразрешённое имя несёт свою ошибку и
// остальным не мешает: в конфиг уедет то, что разрешилось.
type NameResult struct {
	Name      string
	Addresses []string
	Error     string
}

// Bootstrap contains physical-network DNS servers already discovered by the caller.
// An empty list explicitly disables hostname bootstrap and local/DHCP probes.
func Run(ctx context.Context, egress scan.Egress, targets []Target, name string, extraNames []string,
	timeout time.Duration, bootstrap []netip.AddrPort) ([]Result, error) {
	if len(targets) > 64 {
		return nil, errors.New("too many DNS probe targets")
	}
	name = strings.TrimSpace(name)
	if _, valid := dns.IsDomainName(dns.Fqdn(name)); name == "" || !valid {
		return nil, errors.New("invalid DNS probe name")
	}
	if len(extraNames) > 0 {
		// Остальные имена спрашиваются у одного, уже выбранного резолвера: круг
		// по всем кандидатам разом дал бы десятки запросов в секунду с одного
		// адреса, и отказать на это резолвер вправе.
		if len(targets) != 1 {
			return nil, errors.New("extra DNS probe names need exactly one target")
		}
		if len(extraNames) > 64 {
			return nil, errors.New("too many extra DNS probe names")
		}
		trimmed := make([]string, 0, len(extraNames))
		for _, extra := range extraNames {
			extra = strings.TrimSpace(extra)
			if _, valid := dns.IsDomainName(dns.Fqdn(extra)); extra == "" || !valid {
				return nil, errors.New("invalid extra DNS probe name")
			}
			trimmed = append(trimmed, extra)
		}
		extraNames = trimmed
	}
	if timeout <= 0 || timeout > 10*time.Second {
		return nil, errors.New("DNS probe timeout must be between 1 ms and 10 s")
	}
	control, err := egress.Control()
	if err != nil {
		return nil, err
	}
	if control == nil {
		return nil, scan.ErrNoEgress
	}
	results := make([]Result, len(targets))
	var wg sync.WaitGroup
	for i, target := range targets {
		wg.Add(1)
		go func() {
			defer wg.Done()
			probeCtx, cancel := context.WithTimeout(ctx, timeout)
			defer cancel()
			results[i] = probe(probeCtx, egress, target, name, extraNames, bootstrap)
		}()
	}
	wg.Wait()
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	return results, nil
}

func probe(ctx context.Context, egress scan.Egress, target Target, name string, extraNames []string,
	bootstrap []netip.AddrPort) Result {
	result := Result{Address: target.Address, LatencyMS: -1}
	begin := time.Now()
	if target.Type == "local" || target.Type == "dhcp" {
		if len(bootstrap) == 0 {
			result.Error = "physical network DNS is unavailable"
			return result
		}
		if target.Type == "dhcp" && target.Host != "" && target.Host != "auto" {
			result.Error = "interface-specific DHCP probing is not supported"
			return result
		}
		for _, server := range bootstrap {
			attempt := Target{Address: target.Address, Type: "udp", Host: server.Addr().String(), Port: server.Port()}
			result = probe(ctx, egress, attempt, name, extraNames, nil)
			if result.Responded || ctx.Err() != nil {
				break
			}
		}
		return result
	}
	host := strings.Trim(target.Host, "[]")
	ip, err := netip.ParseAddr(host)
	if err != nil {
		if len(bootstrap) == 0 {
			result.Error = "no physical DNS available to bootstrap resolver hostname"
			return result
		}
		for _, server := range bootstrap {
			resolved := probe(ctx, egress, Target{Type: "udp", Host: server.Addr().String(), Port: server.Port()}, host, nil, nil)
			if resolved.OK {
				ip = netip.MustParseAddr(resolved.Addresses[0])
				break
			}
			if ctx.Err() != nil {
				break
			}
		}
		if !ip.IsValid() {
			result.Error = "resolver hostname did not resolve through physical DNS"
			return result
		}
	}
	server := target
	server.Host = ip.String()
	dialer := &physicalDialer{egress: egress}
	probeCtx := service.ContextWith[adapter.OutboundManager](ctx, outboundManager{outbound: physicalOutbound{Dialer: dialer}})
	upstream, err := newTransport(probeCtx, server, host)
	if err != nil {
		result.Error = err.Error()
		return result
	}
	scope := adapter.NewScope(probeCtx, log.NewNOPFactory().NewLogger("dns-probe"))
	defer scope.Close()
	if err = upstream.Start(adapter.StartStateStart, scope); err != nil {
		result.Error = err.Error()
		return result
	}
	addresses, responded, failure := exchange(probeCtx, upstream, target.Type, name)
	result.Responded = responded
	if responded {
		result.LatencyMS = int32(time.Since(begin).Milliseconds())
	}
	result.Addresses = addresses
	result.Error = failure
	result.OK = failure == "" && len(addresses) > 0
	if !result.OK {
		return result
	}
	// Транспорт уже поднят, и соединение к резолверу живо: остальные имена
	// стоят по запросу каждое, а не по кругу проб. Ошибка одного имени
	// записывается ему же и остальных не отменяет.
	for _, extra := range extraNames {
		found, _, extraFailure := exchange(probeCtx, upstream, target.Type, extra)
		result.Extra = append(result.Extra, NameResult{Name: extra, Addresses: found, Error: extraFailure})
	}
	return result
}

// Спрашивает A и AAAA по одному имени через уже поднятый транспорт. responded
// отвечает на другой вопрос, чем addresses: отказ NXDOMAIN адресов не даёт, а
// резолвер при этом жив, и подбор прямого DNS различает это одно от другого.
func exchange(ctx context.Context, upstream adapter.DNSTransport, kind string,
	name string) (addresses []string, responded bool, failure string) {
	for _, qtype := range []uint16{dns.TypeA, dns.TypeAAAA} {
		query := new(dns.Msg)
		query.SetQuestion(dns.Fqdn(name), qtype)
		response, err := upstream.Exchange(ctx, query)
		if err != nil {
			return nil, responded, err.Error()
		}
		expectedID := query.Id
		if kind == "https" || kind == "h3" || kind == "quic" {
			expectedID = 0
		}
		if response == nil || !response.Response || response.Id != expectedID || len(response.Question) != 1 || !strings.EqualFold(response.Question[0].Name, query.Question[0].Name) || response.Question[0].Qtype != qtype || response.Question[0].Qclass != dns.ClassINET {
			return nil, responded, "invalid DNS response"
		}
		responded = true
		if response.Rcode != dns.RcodeSuccess {
			return nil, true, "DNS " + dns.RcodeToString[response.Rcode]
		}
		if found := answerAddresses(response, name); len(found) > 0 {
			return found, true, ""
		}
	}
	return nil, responded, "DNS response contains no address"
}

func answerAddresses(response *dns.Msg, name string) []string {
	owners := map[string]bool{strings.ToLower(dns.Fqdn(name)): true}
	for range len(response.Answer) {
		for _, rr := range response.Answer {
			if c, ok := rr.(*dns.CNAME); ok && owners[strings.ToLower(c.Hdr.Name)] {
				owners[strings.ToLower(c.Target)] = true
			}
		}
	}
	var addresses []string
	seen := make(map[string]bool)
	for _, rr := range response.Answer {
		if !owners[strings.ToLower(rr.Header().Name)] {
			continue
		}
		var ip net.IP
		switch rr := rr.(type) {
		case *dns.A:
			ip = rr.A
		case *dns.AAAA:
			ip = rr.AAAA
		}
		if addr, ok := netip.AddrFromSlice(ip); ok && !seen[addr.Unmap().String()] {
			address := addr.Unmap().String()
			seen[address] = true
			addresses = append(addresses, address)
		}
	}
	return addresses
}

type physicalDialer struct{ egress scan.Egress }

func (d *physicalDialer) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	if !destination.IsIP() {
		return nil, errors.New("direct DNS dial requires a resolved IP")
	}
	bind, err := d.egress.Control()
	if err != nil {
		return nil, err
	}
	if bind == nil {
		return nil, scan.ErrNoEgress
	}
	return (&net.Dialer{Control: bind}).DialContext(ctx, network, destination.String())
}
func (d *physicalDialer) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	if !destination.IsIP() {
		return nil, errors.New("direct DNS packet socket requires a resolved IP")
	}
	bind, err := d.egress.Control()
	if err != nil {
		return nil, err
	}
	if bind == nil {
		return nil, scan.ErrNoEgress
	}
	network, address := "udp4", "0.0.0.0:0"
	if destination.IsIPv6() {
		network, address = "udp6", "[::]:0"
	}
	lc := net.ListenConfig{Control: func(network, _ string, conn syscall.RawConn) error { return bind(network, destination.String(), conn) }}
	return lc.ListenPacket(ctx, network, address)
}

type physicalOutbound struct{ N.Dialer }

func (physicalOutbound) Type() string           { return "dns-probe" }
func (physicalOutbound) Tag() string            { return directTag }
func (physicalOutbound) Network() []string      { return []string{"tcp", "udp"} }
func (physicalOutbound) Dependencies() []string { return nil }

type outboundManager struct {
	adapter.OutboundManager
	outbound adapter.Outbound
}

func (m outboundManager) Outbound(tag string) (adapter.Outbound, bool) {
	return m.outbound, tag == directTag
}

func newTransport(ctx context.Context, target Target, serverName string) (adapter.DNSTransport, error) {
	logger := log.NewNOPFactory().NewLogger("dns-probe")
	remote := option.RemoteDNSServerOptions{DNSServerAddressOptions: option.DNSServerAddressOptions{Server: target.Host, ServerPort: target.Port}, RawLocalDNSServerOptions: option.RawLocalDNSServerOptions{DialerOptions: option.DialerOptions{Detour: directTag}}}
	tlsOptions := option.RemoteTLSDNSServerOptions{RemoteDNSServerOptions: remote, OutboundTLSOptionsContainer: option.OutboundTLSOptionsContainer{TLS: &option.OutboundTLSOptions{Enabled: true, ServerName: serverName}}}
	httpsOptions := option.RemoteHTTPSDNSServerOptions{RemoteTLSDNSServerOptions: tlsOptions, Path: target.Path}
	switch target.Type {
	case "udp":
		return transport.NewUDP(ctx, logger, "probe", remote)
	case "tcp":
		return transport.NewTCP(ctx, logger, "probe", remote)
	case "tls":
		return transport.NewTLS(ctx, logger, "probe", tlsOptions)
	case "https":
		return transport.NewHTTPS(ctx, logger, "probe", httpsOptions)
	case "quic":
		return quictransport.NewQUIC(ctx, logger, "probe", tlsOptions)
	case "h3":
		return quictransport.NewHTTP3(ctx, logger, "probe", httpsOptions)
	default:
		return nil, fmt.Errorf("unsupported DNS transport %q", target.Type)
	}
}
