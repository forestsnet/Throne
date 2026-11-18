package xraydns

import (
	"context"
	"errors"
	"sync/atomic"
	"testing"
	"time"

	"ThroneCore/internal/boxbox"
	"ThroneCore/internal/boxmain"
	"ThroneCore/internal/dnspins"

	C "github.com/sagernet/sing-box/constant"
	tun "github.com/sagernet/sing-tun"
	E "github.com/sagernet/sing/common/exceptions"
	dnsfeature "github.com/xtls/xray-core/features/dns"
)

// Ожидание коробки длится секундами, а тесту нужен только факт ожидания.
func shortWait(t *testing.T, wait time.Duration) {
	t.Helper()
	previous := readyWait
	readyWait = wait
	t.Cleanup(func() { readyWait = previous })
}

// A rule sends every query to dns-remote, so a dns-direct answer proves the pin beat the rule chain.
const splitAnswerConfig = `{
  "log": {"disabled": true},
  "dns": {
    "servers": [
      {"type": "hosts", "tag": "dns-direct", "predefined": {"pinned.invalid": ["10.0.0.1"]}},
      {"type": "hosts", "tag": "dns-remote", "predefined": {"pinned.invalid": ["10.0.0.2"]}}
    ],
    "rules": [{"domain": "pinned.invalid", "action": "route", "server": "dns-remote"}],
    "final": "dns-remote"
  },
  "outbounds": [{"type": "direct", "tag": "direct"}]
}`

// Same, minus dns-direct: what a user-supplied raw DNS object can look like.
const noDirectConfig = `{
  "log": {"disabled": true},
  "dns": {
    "servers": [
      {"type": "hosts", "tag": "dns-remote", "predefined": {"pinned.invalid": ["10.0.0.2"]}}
    ],
    "final": "dns-remote"
  },
  "outbounds": [{"type": "direct", "tag": "direct"}]
}`

func startBox(t *testing.T, config string) *boxbox.Box {
	t.Helper()
	box, cancel, err := boxmain.Create([]byte(config), nil)
	if err != nil {
		t.Fatalf("create box: %v", err)
	}
	t.Cleanup(func() {
		_ = box.Close()
		cancel()
	})
	return box
}

func lookupV4(t *testing.T, box *boxbox.Box, domain string) []string {
	t.Helper()
	resolver := New(func() context.Context { return box.Context() })
	ips, _, err := resolver.LookupIP(domain, dnsfeature.IPOption{IPv4Enable: true})
	if err != nil {
		t.Fatalf("lookup %s: %v", domain, err)
	}
	out := make([]string, 0, len(ips))
	for _, ip := range ips {
		out = append(out, ip.String())
	}
	return out
}

func TestLookupPinsDNSDirectOverRules(t *testing.T) {
	got := lookupV4(t, startBox(t, splitAnswerConfig), "pinned.invalid")
	if len(got) != 1 || got[0] != "10.0.0.1" {
		t.Fatalf("expected the dns-direct answer 10.0.0.1, got %v", got)
	}
}

func TestLookupFallsBackToRulesWithoutDNSDirect(t *testing.T) {
	got := lookupV4(t, startBox(t, noDirectConfig), "pinned.invalid")
	if len(got) != 1 || got[0] != "10.0.0.2" {
		t.Fatalf("expected the rule-selected answer 10.0.0.2, got %v", got)
	}
}

// A lookup can legitimately arrive before the box exists; it must report, not panic.
func TestLookupBeforeBoxExists(t *testing.T) {
	shortWait(t, 50*time.Millisecond)
	resolver := New(func() context.Context { return nil })
	if _, _, err := resolver.LookupIP("pinned.invalid", dnsfeature.IPOption{IPv4Enable: true}); err == nil {
		t.Fatal("expected an error while no box is up")
	}
}

// Xray спрашивает адрес сервера раньше, чем поднялась коробка. Отказать сразу
// значит отдать ему «узел недоступен» в единственный момент, когда это неправда.
func TestLookupWaitsForBoxToAppear(t *testing.T) {
	shortWait(t, 5*time.Second)
	box := startBox(t, splitAnswerConfig)
	var calls atomic.Int32
	resolver := New(func() context.Context {
		// Первые обращения застают приложение до коробки, как на старте.
		if calls.Add(1) < 4 {
			return nil
		}
		return box.Context()
	})
	ips, _, err := resolver.LookupIP("pinned.invalid", dnsfeature.IPOption{IPv4Enable: true})
	if err != nil {
		t.Fatalf("ждали ответа после появления коробки, получили: %v", err)
	}
	if len(ips) != 1 || ips[0].String() != "10.0.0.1" {
		t.Fatalf("адреса: %v", ips)
	}
	if calls.Load() < 4 {
		t.Fatalf("провайдера спросили %d раз, то есть не дождались", calls.Load())
	}
}

// Ожидание не бесконечно: коробки может не быть вовсе, и тогда надо отказать.
func TestLookupGivesUpWaitingForBox(t *testing.T) {
	shortWait(t, 120*time.Millisecond)
	resolver := New(func() context.Context { return nil })
	started := time.Now()
	if _, _, err := resolver.LookupIP("pinned.invalid", dnsfeature.IPOption{IPv4Enable: true}); err == nil {
		t.Fatal("ждали отказа")
	}
	if waited := time.Since(started); waited < 100*time.Millisecond {
		t.Fatalf("сдались через %v, то есть не ждали вовсе", waited)
	}
}

// Закреплённое имя отвечается до всякого ожидания — иначе на старте каждое
// соединение к своему же серверу стояло бы в очереди за коробкой. Ожидание
// выкручено в час: если ответ придёт, значит до него дело и не дошло.
func TestPinnedNameAnswersWithoutWaiting(t *testing.T) {
	shortWait(t, time.Hour)
	dnspins.Set(map[string][]string{"pinned.invalid": {"10.0.0.9"}})
	t.Cleanup(dnspins.Clear)
	resolver := New(func() context.Context { return nil })
	started := time.Now()
	ips, _, err := resolver.LookupIP("pinned.invalid", dnsfeature.IPOption{IPv4Enable: true})
	if err != nil {
		t.Fatalf("закреплённое имя должно отвечаться и без коробки: %v", err)
	}
	if len(ips) != 1 || ips[0].String() != "10.0.0.9" {
		t.Fatalf("адреса: %v", ips)
	}
	if waited := time.Since(started); waited > time.Second {
		t.Fatalf("ждали %v вместо немедленного ответа", waited)
	}
}

// Пока монитор интерфейсов не нашёл сеть, отказ приходит с самого сокета. Текст
// приезжает завёрнутым, и узнать его надо в любом виде.
func TestNoRouteYet(t *testing.T) {
	for _, tc := range []struct {
		name string
		err  error
		want bool
	}{
		{"сама переменная", tun.ErrNoRoute, true},
		{"завёрнута через sing", E.Cause(tun.ErrNoRoute, "dial tcp 77.88.8.8:853"), true},
		{"только текст, как в журнале", errors.New("dial TLS connection: dial tcp 77.88.8.8:853: no route to internet"), true},
		{"посторонняя ошибка", errors.New("i/o timeout"), false},
		{"отказ ядра при остановке", errors.New("dns router closed"), false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if got := noRouteYet(tc.err); got != tc.want {
				t.Fatalf("got %v, want %v", got, tc.want)
			}
		})
	}
}

// LookupForIPWithClient asks for neither family when the strategy and the bound local address disagree.
func TestLookupWithNoFamilyEnabled(t *testing.T) {
	resolver := New(func() context.Context { return startBox(t, splitAnswerConfig).Context() })
	_, _, err := resolver.LookupIP("pinned.invalid", dnsfeature.IPOption{})
	if !errors.Is(err, dnsfeature.ErrEmptyResponse) {
		t.Fatalf("expected ErrEmptyResponse, got %v", err)
	}
}

func TestStrategyFor(t *testing.T) {
	for _, tc := range []struct {
		name   string
		option dnsfeature.IPOption
		want   C.DomainStrategy
	}{
		{"both defers to the box default", dnsfeature.IPOption{IPv4Enable: true, IPv6Enable: true}, C.DomainStrategyAsIS},
		{"v4 only", dnsfeature.IPOption{IPv4Enable: true}, C.DomainStrategyIPv4Only},
		{"v6 only", dnsfeature.IPOption{IPv6Enable: true}, C.DomainStrategyIPv6Only},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if got := strategyFor(tc.option); got != tc.want {
				t.Fatalf("got %v, want %v", got, tc.want)
			}
		})
	}
}
