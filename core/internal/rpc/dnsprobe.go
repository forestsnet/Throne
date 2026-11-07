package rpc

import (
	"context"
	"net/netip"
	"slices"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/dnsprobe"
	"ThroneCore/internal/netmon"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/dns/transport/local/systemconfig"
	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/service"
)

// The system DNS reader needs a monitor, not a running profile or its DNS router.
type probeNetwork struct {
	adapter.NetworkManager
	monitor tun.DefaultInterfaceMonitor
}

func (n probeNetwork) InterfaceMonitor() tun.DefaultInterfaceMonitor { return n.monitor }

func physicalDNSServers(ctx context.Context) []netip.AddrPort {
	monitor := netmon.Monitor()
	if box := currentBox(); box != nil {
		monitor = box.Network().InterfaceMonitor()
	}
	ctx = service.ContextWith[adapter.NetworkManager](ctx, probeNetwork{monitor: monitor})
	source := systemconfig.NewSource(ctx)
	defer source.Close()
	var servers []netip.AddrPort
	for _, server := range source.Configuration().Servers {
		if !server.IsIP() || server.Addr.IsLoopback() || server.Addr.IsUnspecified() || server.Addr.IsMulticast() {
			continue
		}
		if finder := netmon.Finder(); finder != nil && monitor != nil {
			if iif, err := finder.ByAddr(server.Addr); err == nil && slices.Contains(monitor.MyInterfaces(), iif.Name) {
				continue
			}
		}
		servers = append(servers, server.AddrPort())
	}
	return servers
}

func (s *server) ProbeDNS(ctx context.Context, in *gen.DNSProbeRequest) (*gen.DNSProbeResponse, error) {
	response := &gen.DNSProbeResponse{}
	iface := netmon.DefaultInterface()
	if iface == nil {
		response.Error = To("no physical default interface")
		return response, nil
	}
	response.InterfaceName = To(iface.Name)
	response.InterfaceIndex = To(int32(iface.Index))
	lost, stop := watchScanEgress()
	defer stop()
	var targets []dnsprobe.Target
	for _, target := range in.GetTargets() {
		if target.GetPort() > 65535 {
			response.Error = To("invalid DNS probe port")
			return response, nil
		}
		targets = append(targets, dnsprobe.Target{Address: target.GetAddress(), Type: target.GetType(), Host: target.GetHost(), Port: uint16(target.GetPort()), Path: target.GetPath()})
	}
	results, err := dnsprobe.Run(ctx, scanEgress{requireBinding: true}, targets, in.GetProbeName(), time.Duration(in.GetTimeoutMs())*time.Millisecond, physicalDNSServers(ctx))
	if err != nil {
		response.Error = To(err.Error())
		return response, nil
	}
	current := netmon.DefaultInterface()
	if lost() || current == nil || current.Name != iface.Name || current.Index != iface.Index {
		response.Error = To("physical network changed during DNS probe")
		return response, nil
	}
	for _, result := range results {
		response.Results = append(response.Results, &gen.DNSProbeResult{Address: To(result.Address), Responded: To(result.Responded), Ok: To(result.OK), LatencyMs: To(result.LatencyMS), Error: To(result.Error), Addresses: result.Addresses})
	}
	return response, nil
}
