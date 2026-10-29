// Package dnspins holds the addresses the application resolved before the tunnel came up.
//
// The direct resolver is asked for our own server name by every new connection, not just the
// first, and under the tunnel there may be nothing left to ask: on Windows strict_route installs
// a firewall filter against port 53 on every interface but the tunnel's, an encrypted resolver
// named by hostname cannot be bootstrapped once the system resolver is captured, and where our
// users are DoT and DoH are themselves filtered often enough that picking one is a bet.
//
// The application already asks that name before it connects - that is how it tells a resolver
// that cannot answer for it from one that can - and at that moment nothing is blocked yet. What
// it learned arrives with the config and is answered from here, so the question is never asked
// a second time.
//
// A DNS rule cannot do this job: the Xray bridge and a sing-box outbound's own domain_resolver
// both pin a transport, and the router skips rule matching whenever a transport is pinned.
package dnspins

import (
	"net/netip"
	"strings"
	"sync"
)

var (
	access sync.RWMutex
	pins   map[string][]netip.Addr
)

// Set replaces the table. Addresses that do not parse are dropped: a pin is an answer we already
// have, so there is nothing to fall back to and no reason to fail a connection over one bad entry.
func Set(entries map[string][]string) {
	next := make(map[string][]netip.Addr, len(entries))
	for domain, addresses := range entries {
		name := normalize(domain)
		if name == "" {
			continue
		}
		for _, address := range addresses {
			parsed, err := netip.ParseAddr(strings.TrimSpace(address))
			if err != nil {
				continue
			}
			next[name] = append(next[name], parsed.Unmap())
		}
	}
	access.Lock()
	defer access.Unlock()
	pins = next
}

// Clear drops the table. A subscription moves, so a pin is worth exactly one session.
func Clear() {
	access.Lock()
	defer access.Unlock()
	pins = nil
}

// Lookup returns the addresses of the given family, or nil when the name is not pinned.
// Both families disabled is not a lookup, and is answered as a miss.
func Lookup(domain string, wantV4, wantV6 bool) []netip.Addr {
	if !wantV4 && !wantV6 {
		return nil
	}
	name := normalize(domain)
	if name == "" {
		return nil
	}
	access.RLock()
	defer access.RUnlock()
	addresses, found := pins[name]
	if !found {
		return nil
	}
	picked := make([]netip.Addr, 0, len(addresses))
	for _, address := range addresses {
		if address.Is4() && wantV4 || address.Is6() && wantV6 {
			picked = append(picked, address)
		}
	}
	if len(picked) == 0 {
		// The name is pinned, just not for this family. Saying so would hide the other one,
		// so the caller is left to ask as it would have anyway.
		return nil
	}
	return picked
}

func normalize(domain string) string {
	return strings.ToLower(strings.TrimSuffix(strings.TrimSpace(domain), "."))
}
