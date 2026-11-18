// Package xraydns resolves Xray outbound server domains through the sing-box instance in this same process.
//
// Speaking DNS to sing-box's `dns-in` inbound made outbound resolution depend on an inbound, and inbounds start
// only after the router initializes its rule-sets - so the first remote rule-set download deadlocked against the
// proxy server domain it needed to dial (#1811). Pinning `dns-direct` in DNSQueryOptions is what a sing-box
// outbound's own `domain_resolver` does, minus the socket.
package xraydns

import (
	"context"
	"errors"
	"net/netip"
	"strings"
	"sync"
	"time"

	"ThroneCore/internal/dnspins"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	tun "github.com/sagernet/sing-tun"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/service"
	"github.com/xtls/xray-core/common/net"
	dnsfeature "github.com/xtls/xray-core/features/dns"
)

// Matches tags::dnsDirect in the config builder (src/configs/generate.cpp).
const directTransportTag = "dns-direct"

const lookupTimeout = 5 * time.Second

// Xray поднимается раньше sing-box и спрашивает адрес сервера сразу, так что
// отказ в эту секунду — это норма работы, а не поломка. До появления этого
// ожидания такой запрос падал мгновенно: пока нет коробки — "sing-box instance
// is not up yet", а как появилась, но монитор интерфейсов ещё не нашёл сеть —
// tun.ErrNoRoute с самого сокета. Xray тратил на эти отказы свои попытки за
// миллисекунды и сдавался, а системный резолвер потом минутами повторял одни и
// те же запросы, которым никто не отвечал.
//
// Замер по журналам с машин клиента 18.11.2026: между первой строкой Xray и
// "sing-box started" прошло 2 секунды на одной машине и 0.3 на другой. Ждём с
// запасом, и только на старте — дальше имя уже либо в таблице закреплённых,
// либо в кэше ядра.
//
// Переменные, а не константы: тесты укорачивают ожидание.
var (
	readyWait = 5 * time.Second
	readyPoll = 25 * time.Millisecond
)

// Read per lookup, not captured: the box is always built after the Xray instances that resolve through it.
type BoxProvider func() context.Context

type resolver struct {
	provider BoxProvider

	access    sync.Mutex
	transport adapter.DNSTransport
}

func New(provider BoxProvider) dnsfeature.Client {
	return &resolver{provider: provider}
}

func (*resolver) Type() interface{} { return dnsfeature.ClientType() }
func (*resolver) Start() error      { return nil }
func (*resolver) Close() error      { return nil }

func (r *resolver) LookupIP(domain string, option dnsfeature.IPOption) ([]net.IP, uint32, error) {
	if !option.IPv4Enable && !option.IPv6Enable {
		// Answering would defeat the dialer's own fallback pass.
		return nil, 0, dnsfeature.ErrEmptyResponse
	}
	// Раньше коробки и раньше всего остального: имя уже разрешено приложением,
	// спрашивать его второй раз неоткуда и незачем. Заодно снимает отказ
	// «sing-box instance is not up yet» для самого важного имени — адреса
	// сервера, который нужен как раз в тот момент, когда коробка ещё поднимается.
	if pinned := dnspins.Lookup(domain, option.IPv4Enable, option.IPv6Enable); len(pinned) > 0 {
		ips := make([]net.IP, 0, len(pinned))
		for _, address := range pinned {
			ips = append(ips, net.IP(address.Unmap().AsSlice()))
		}
		return ips, 0, nil
	}
	deadline := time.Now().Add(readyWait)
	ctx, err := waitForBox(r.provider, deadline)
	if err != nil {
		return nil, 0, err
	}
	router := service.FromContext[adapter.DNSRouter](ctx)
	if router == nil {
		return nil, 0, E.New("throne-dns: sing-box DNS router is not registered")
	}
	addresses, err := r.lookupOnceRouted(ctx, router, domain, option, deadline)
	if err != nil {
		return nil, 0, err
	}
	if len(addresses) == 0 {
		return nil, 0, dnsfeature.ErrEmptyResponse
	}
	ips := make([]net.IP, 0, len(addresses))
	for _, address := range addresses {
		// Unmap: net.IPAddress expects 4 bytes for a v4 answer.
		ips = append(ips, net.IP(address.Unmap().AsSlice()))
	}
	// LookupForIPWithClient discards the TTL and sing-box owns the cache.
	return ips, 0, nil
}

// Ждёт коробку: она создаётся после инстансов Xray, которые через неё резолвят,
// поэтому первые запросы приходят раньше неё всегда.
func waitForBox(provider BoxProvider, deadline time.Time) (context.Context, error) {
	for {
		if ctx := provider(); ctx != nil {
			return ctx, nil
		}
		if !time.Now().Before(deadline) {
			return nil, E.New("throne-dns: sing-box instance is not up yet")
		}
		time.Sleep(readyPoll)
	}
}

// Прямой запрос уходит с сокета, прибитого к интерфейсу по умолчанию, и пока
// монитор его не нашёл, control-функция отвечает tun.ErrNoRoute
// (sing-box route/network.go, AutoDetectInterfaceFunc). Отказ приходит на самом
// сокете, не выходя в сеть, то есть переспросить стоит доли миллисекунды —
// дешевле, чем отдать Xray ошибку, которую тот примет за недоступный узел.
func (r *resolver) lookupOnceRouted(ctx context.Context, router adapter.DNSRouter, domain string,
	option dnsfeature.IPOption, deadline time.Time) ([]netip.Addr, error) {
	for {
		addresses, err := router.Lookup(ctx, domain, adapter.DNSQueryOptions{
			Transport: r.directTransport(ctx),
			Strategy:  strategyFor(option),
			Timeout:   lookupTimeout,
		})
		if err == nil || !noRouteYet(err) || !time.Now().Before(deadline) {
			return addresses, err
		}
		time.Sleep(readyPoll)
	}
}

// tun.ErrNoRoute приезжает завёрнутым в несколько слоёв — в журнале это
// "dial TLS connection: dial tcp 77.88.8.8:853: no route to internet", — и
// ручаться за Unwrap по всей цепочке нельзя. Поэтому сверяем и через errors.Is,
// и по тексту самой библиотечной переменной: так проверка не разойдётся с ней
// при обновлении sing-tun.
func noRouteYet(err error) bool {
	return errors.Is(err, tun.ErrNoRoute) || strings.Contains(err.Error(), tun.ErrNoRoute.Error())
}

// nil leaves the query unpinned and on the config's own DNS rules, which is what a raw DNS object asks for.
func (r *resolver) directTransport(ctx context.Context) adapter.DNSTransport {
	r.access.Lock()
	defer r.access.Unlock()
	if r.transport != nil {
		return r.transport
	}
	manager := service.FromContext[adapter.DNSTransportManager](ctx)
	if manager == nil {
		return nil
	}
	transport, loaded := manager.Transport(directTransportTag)
	if !loaded {
		return nil
	}
	r.transport = transport
	return transport
}

func strategyFor(option dnsfeature.IPOption) C.DomainStrategy {
	switch {
	case option.IPv4Enable && option.IPv6Enable:
		// Defers to route.default_domain_resolver.strategy.
		return C.DomainStrategyAsIS
	case option.IPv4Enable:
		return C.DomainStrategyIPv4Only
	default:
		return C.DomainStrategyIPv6Only
	}
}
