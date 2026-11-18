package probe

import (
	"testing"
	"time"
)

func TestColdRequestGetsTheSetupAllowance(t *testing.T) {
	if got := firstRequestTimeout(true, 3*time.Second); got != 3*time.Second+ColdStartAllowance {
		t.Fatalf("a cold request got %s, the per-request timeout plus the allowance was expected", got)
	}
	if got := firstRequestTimeout(false, 3*time.Second); got != 3*time.Second {
		t.Fatalf("a warm request got %s, it rides an open connection and gets the plain timeout", got)
	}
}
