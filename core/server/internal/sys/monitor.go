package sys

import (
	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
	"github.com/sagernet/sing/common/logger"
)

func GetDefaultInterfaceMonitor() tun.DefaultInterfaceMonitor {
	logger := logger.NOP()
	nw, err := tun.NewNetworkUpdateMonitor(logger)
	if err != nil {
		return nil
	}
	mon, err := tun.NewDefaultInterfaceMonitor(nw, logger, tun.DefaultInterfaceMonitorOptions{
		InterfaceFinder: control.NewDefaultInterfaceFinder(),
	})
	if err != nil {
		return nil
	}
	return mon
}
