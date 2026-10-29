package dnspins

import (
	"net/netip"
	"testing"
)

func addrs(t *testing.T, values ...string) []netip.Addr {
	t.Helper()
	out := make([]netip.Addr, 0, len(values))
	for _, value := range values {
		out = append(out, netip.MustParseAddr(value))
	}
	return out
}

func equal(got, want []netip.Addr) bool {
	if len(got) != len(want) {
		return false
	}
	for i := range got {
		if got[i] != want[i] {
			return false
		}
	}
	return true
}

func TestLookupMatchesByFamily(t *testing.T) {
	defer Clear()
	Set(map[string][]string{
		"relay.example.org": {"203.0.113.7", "2001:db8::1"},
	})

	if got := Lookup("relay.example.org", true, false); !equal(got, addrs(t, "203.0.113.7")) {
		t.Fatalf("v4 only: got %v", got)
	}
	if got := Lookup("relay.example.org", false, true); !equal(got, addrs(t, "2001:db8::1")) {
		t.Fatalf("v6 only: got %v", got)
	}
	if got := Lookup("relay.example.org", true, true); len(got) != 2 {
		t.Fatalf("both families: got %v", got)
	}
	// Ни одного семейства — не вопрос, а значит и не ответ: у вызывающего
	// свой проход по запасным вариантам, и перебивать его нечем.
	if got := Lookup("relay.example.org", false, false); got != nil {
		t.Fatalf("no family asked: got %v", got)
	}
}

// Имя приходит и от Xray, и от приложения, и регистр с точкой на конце у них
// разный. Промах из-за этого означал бы запрос в сеть, которого мы и избегаем.
func TestLookupIgnoresCaseAndTrailingDot(t *testing.T) {
	defer Clear()
	Set(map[string][]string{"Relay.Example.ORG.": {"203.0.113.7"}})

	for _, name := range []string{"relay.example.org", "RELAY.EXAMPLE.ORG", "relay.example.org."} {
		if got := Lookup(name, true, false); len(got) != 1 {
			t.Fatalf("%s: got %v", name, got)
		}
	}
}

// Запись, которая есть, но не того семейства, не должна выдаваться за ответ:
// иначе вызывающий получит пусто там, где настоящий резолвер ответил бы.
func TestPinnedButWrongFamilyIsAMiss(t *testing.T) {
	defer Clear()
	Set(map[string][]string{"v4only.example.org": {"203.0.113.7"}})

	if got := Lookup("v4only.example.org", false, true); got != nil {
		t.Fatalf("expected a miss, got %v", got)
	}
}

func TestBadInputIsDroppedNotFatal(t *testing.T) {
	defer Clear()
	Set(map[string][]string{
		"":                   {"203.0.113.1"},
		"broken.example.org": {"not-an-address", "203.0.113.9"},
	})

	if got := Lookup("broken.example.org", true, false); !equal(got, addrs(t, "203.0.113.9")) {
		t.Fatalf("got %v", got)
	}
	if got := Lookup("", true, false); got != nil {
		t.Fatalf("empty name: got %v", got)
	}
}

func TestClearForgetsEverything(t *testing.T) {
	Set(map[string][]string{"relay.example.org": {"203.0.113.7"}})
	Clear()
	if got := Lookup("relay.example.org", true, true); got != nil {
		t.Fatalf("got %v", got)
	}
}
