package rpc

import (
	"context"
	"errors"
	"fmt"
	"log"
	"runtime"
	"strings"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/boxmain"
	"ThroneCore/internal/dnspins"
	"ThroneCore/internal/process"
	"ThroneCore/internal/sysdns"
	"ThroneCore/internal/sysrelay"
	"ThroneCore/internal/xray"

	"github.com/google/shlex"
	"github.com/sagernet/sing-box/option"
	E "github.com/sagernet/sing/common/exceptions"
)

func (s *server) Start(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()

	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
			setBoxInstance(nil, nil, nil)
			autoRedirectMark.Store(0)
		}
	}()

	if debugLog {
		log.Println("Start:", *in.CoreConfig)
		if in.XrayConfig != nil {
			log.Println("Start Xray:", *in.XrayConfig)
		}
	}

	if currentBox() != nil {
		err = errors.New("instance already started")
		return
	}

	if *in.NeedExtraProcess {
		args, e := shlex.Split(in.GetExtraProcessArgs())
		if e != nil {
			err = E.Cause(e, "Failed to parse args")
			return
		}
		var extraConfPath, extraCleanupPath string
		if in.ExtraProcessConf != nil {
			extraConfPath, extraCleanupPath, e = process.CreateExtraConfig(*in.ExtraProcessConf)
			if e != nil {
				err = E.Cause(e, "Failed to create extra.conf")
				return
			}
			for idx, arg := range args {
				if strings.Contains(arg, "%s") {
					args[idx] = fmt.Sprintf(arg, extraConfPath)
					break
				}
			}
		}

		extraProcess = process.NewProcess(*in.ExtraProcessPath, args, *in.ExtraNoOut)
		extraProcess.SetCleanupPath(extraCleanupPath)
		err = extraProcess.Start()
		if err != nil {
			return
		}
	}

	autoRedirectMark.Store(autoRedirectMarkFor([]byte(in.GetCoreConfig())))

	// Filled in below, once boxmain.Create has built the box these sidecars resolve through.
	var boxCtx boxContextHolder
	prepareXray := xrayPreparer(in.GetXrayOutboundDnsStrategy(), boxCtx.get)

	if *in.NeedXray {
		if in.GetXrayLazyStart() {
			gate, e := xray.StartGate(*in.XrayConfig,
				time.Duration(in.GetXrayIdleSeconds())*time.Second, prepareXray)
			if e != nil {
				err = e
				return
			}
			setXray(nil, gate)
		} else {
			instance, e := xray.CreateXrayInstance(*in.XrayConfig)
			if e != nil {
				err = e
				return
			}
			if e = prepareXray(instance); e != nil {
				instance.Close()
				err = e
				return
			}
			if e = instance.Start(); e != nil {
				instance.Close()
				err = e
				return
			}
			setXray(instance, nil)
		}
	}

	if fullConfigs := in.GetXrayFullConfigs(); len(fullConfigs) > 0 {
		gates, e := startXrayFullGates(fullConfigs,
			time.Duration(in.GetXrayFullIdleSeconds())*time.Second, prepareXray)
		if e != nil {
			closeXray()
			err = e
			return
		}
		setXrayFullGates(gates)
	}

	stopSidecars := func() {
		sysdns.Release()
		if extraProcess != nil {
			extraProcess.Stop()
			extraProcess = nil
		}
		closeXray()
	}

	// До создания коробки: Xray поднимается раньше неё и спрашивает адрес сервера
	// сразу, а таблица на то и нужна, чтобы к этому моменту ответ уже лежал.
	pinned := make(map[string][]string, len(in.GetPinnedDomains()))
	for _, pin := range in.GetPinnedDomains() {
		pinned[pin.GetDomain()] = pin.GetAddresses()
	}
	dnspins.Set(pinned)

	// A macOS Tun whose system resolver cannot be pointed at it would start with every lookup scoped past it.
	systemDNS := runtime.GOOS == "darwin" && in.GetTunIpv4Cidr() != ""
	if systemDNS {
		if e := sysdns.Prepare(); e != nil {
			stopSidecars()
			err = E.Cause(e, "set up the system DNS")
			return
		}
	}

	var routeRules []option.Rule
	box, cancel, err := boxmain.Create([]byte(*in.CoreConfig), boxCtx.publish, func(options *option.Options) {
		if options.Route != nil {
			routeRules = options.Route.Rules
		}
	})
	if err != nil {
		stopSidecars()
		return
	}
	setBoxInstance(box, cancel, routeRules)

	if systemDNS {
		if e := sysdns.Apply(box.Network().InterfaceMonitor()); e != nil {
			sysdns.Release()
			box.CloseWithTimeout(cancel, time.Second*2, log.Println, true)
			stopSidecars()
			err = E.Cause(e, "set the system DNS")
			return
		}

		if in.GetDisablePrivateRelay() {
			// Ошибка здесь не повод не подключаться: реле выключается ради
			// честного адреса, а не ради работы туннеля. Вернуть его всё
			// равно попробуем — метку Disable ставит до того, как менять.
			if e := sysrelay.Disable(box.Network().InterfaceMonitor()); e != nil {
				log.Println("Failed to switch iCloud Private Relay off:", e)
			}
		}
	}

	if in.GetTunIpv4Cidr() != "" {
		stopEgressForwardingWatch = watchEgressForwarding(box.Network())
	}

	return
}

// Снимает туннель с его маршрутами, возвращает системный DNS и останавливает
// дочерние процессы — то же, что делает команда Stop из приложения. Отдельная
// точка нужна потому, что это требуется и без команды: по сигналу и по уходу
// родителя ядро обязано убрать за собой само, иначе всё перечисленное остаётся
// в системе, а человек — без сети.
func Teardown(ctx context.Context) error {
	resp, err := globalServer.Stop(ctx, &gen.EmptyReq{})
	if err != nil {
		return err
	}
	if resp != nil && resp.Error != nil && *resp.Error != "" {
		return errors.New(*resp.Error)
	}
	return nil
}

func (s *server) Stop(ctx context.Context, in *gen.EmptyReq) (out *gen.ErrorResp, _ error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()

	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
		}
	}()

	box, cancel := currentInstance()
	if box == nil {
		return
	}

	if stopEgressForwardingWatch != nil {
		stopEgressForwardingWatch()
		stopEgressForwardingWatch = nil
	}

	// Прибитые адреса живут ровно одну сессию: подписка переезжает, и записанный
	// вчера адрес сегодня соврёт.
	dnspins.Clear()

	// Before the box closes, so the system resolver never points at a listener that is already gone.
	sysdns.Release()
	// Без метки это пустой вызов, поэтому спрашивать, выключали ли мы реле,
	// не нужно: отдельный флаг на это только расходился бы с правдой.
	if err := sysrelay.Restore(); err != nil {
		log.Println("Failed to restore iCloud Private Relay:", err)
	}
	// Unpublished first, so a poll mid-teardown sees no instance rather than a dying one.
	setBoxInstance(nil, nil, nil)
	box.CloseWithTimeout(cancel, time.Second*2, log.Println, true)

	if extraProcess != nil {
		extraProcess.Stop()
		extraProcess = nil
	}

	closeXray()
	// The Tun and its nftables rules went down with the box, so later test instances must not carry the exemption mark.
	autoRedirectMark.Store(0)

	return
}

func (s *server) CheckConfig(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	out = &gen.ErrorResp{}
	// boxmain.Check can panic on malformed configs; unrecovered it reaches main()'s os.Exit(0) and kills the core.
	defer func() {
		if r := recover(); r != nil {
			buf := make([]byte, 4096)
			n := runtime.Stack(buf, false)
			log.Printf("CheckConfig panic: %v\n%s", r, buf[:n])
			out.Error = To(fmt.Sprintf("CheckConfig panic: %v", r))
		}
	}()
	if in.GetNeedXray() {
		if err := xray.CheckXrayConfig(in.GetXrayConfig()); err != nil {
			out.Error = To(err.Error())
		}
		return
	}
	err := boxmain.Check([]byte(*in.CoreConfig))
	if err != nil {
		out.Error = To(err.Error())
	}
	return
}
