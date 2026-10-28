//go:build !darwin

package sysrelay

import (
	"errors"

	tun "github.com/sagernet/sing-tun"
)

// Выключатель iCloud Private Relay есть только в macOS.
func Disable(interfaceMonitor tun.DefaultInterfaceMonitor) error {
	return errors.New("not implemented for this OS")
}

func Restore() error { return nil }

func RepairLeftover() {}
