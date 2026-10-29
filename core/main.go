package main

import (
	"context"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"runtime"
	runtimeDebug "runtime/debug"
	"runtime/metrics"
	"runtime/pprof"
	"strings"
	"syscall"
	"time"

	"github.com/xtls/xray-core/core"

	"ThroneCore/internal/boxmain"
	"ThroneCore/internal/capcheck"
	"ThroneCore/internal/guard"
	"ThroneCore/internal/ipc"
	"ThroneCore/internal/parentcheck"
	"ThroneCore/internal/process"
	"ThroneCore/internal/rpc"
	"ThroneCore/internal/sysdns"
	"ThroneCore/internal/sysrelay"

	C "github.com/sagernet/sing-box/constant"

	_ "ThroneCore/internal/distro/all"
)

const (
	// memoryPanicThreshold stays under memoryLimit: that much live under the soft limit means the GC is thrashing.
	memoryLimit           = 2 * 1024 * 1024 * 1024
	memoryPanicThreshold  = 1536 * 1024 * 1024
	memoryCheckInterval   = 2 * time.Second
	memoryForcedGCBackoff = 30 * time.Second
)

// Not HeapAlloc: it counts unswept garbage and sawtooths up to the GC target, so a bare threshold on it fires on a healthy heap.
func liveHeap() uint64 {
	sample := []metrics.Sample{{Name: "/gc/heap/live:bytes"}}
	metrics.Read(sample)
	return sample[0].Value.Uint64()
}

func watchMemory() {
	for {
		time.Sleep(memoryCheckInterval)

		if liveHeap() < memoryPanicThreshold {
			continue
		}

		// The metric is only as fresh as the last cycle, which during a burst can mark short-lived objects live.
		runtimeDebug.FreeOSMemory()
		live := liveHeap()
		if live < memoryPanicThreshold {
			// FreeOSMemory is stop-the-world; do not repeat it every tick while a busy core sits near the threshold.
			time.Sleep(memoryForcedGCBackoff)
			continue
		}

		log.Printf("memory watchdog: %d MiB live after a forced GC, %d goroutines",
			live>>20, runtime.NumGoroutine())
		if path, err := writeHeapProfile(); err != nil {
			log.Printf("memory watchdog: could not write heap profile: %v", err)
		} else {
			log.Printf("memory watchdog: heap profile written to %s", path)
		}
		panic(fmt.Sprintf("Live heap reached %d MiB after a forced GC, this is not normal", live>>20))
	}
}

func writeHeapProfile() (string, error) {
	// Core runs privileged: a guessable clock-derived name lets a planted symlink turn this into a root-owned write anywhere.
	f, err := os.CreateTemp("", "throne-core-heap-*.pprof")
	if err != nil {
		return "", err
	}
	defer f.Close()
	if err = pprof.WriteHeapProfile(f); err != nil {
		return "", err
	}
	return f.Name(), f.Sync()
}

func RunCore() {
	socketName := os.Getenv("THRONE_CORE_SOCKET")
	if socketName == "" {
		log.Fatal("THRONE_CORE_SOCKET not set")
	}
	debug := os.Getenv("THRONE_CORE_DEBUG") == "1"

	parentcheck.CheckParentProcess()
	if err := guard.ApplyIdentity(); err != nil {
		fmt.Fprintf(os.Stderr, "kill switch: cannot adopt the guard group: %v\n", err)
		os.Exit(guard.IdentityExitCode)
	}
	guard.CleanupStale()
	process.SweepStaleConfigDirs()
	sysdns.RestoreStale()
	sysrelay.RepairLeftover()

	go func() {
		parent, err := os.FindProcess(parentcheck.ParentPID)
		if err != nil {
			log.Fatalln("find parent:", err)
		}
		if runtime.GOOS == "windows" {
			state, err := parent.Wait()
			shutdown(1, fmt.Sprintln("parent exited:", state, err))
		} else {
			for {
				time.Sleep(time.Second * 10)
				err = parent.Signal(syscall.Signal(0))
				if err != nil {
					// Не Fatalln: откат живёт в teardown, и системный DNS
					// с реле возвращает он же. Выйти, не позвав его, значит
					// оставить и то, и другое на машине человека.
					shutdown(1, fmt.Sprintln("parent exited:", err))
				}
			}
		}
	}()

	// Сигналы ловим на весь срок жизни, а не только на время создания службы.
	// Без этого SIGTERM убивал ядро по умолчанию, то есть ровно так же, как
	// SIGKILL: туннель, маршруты и системный DNS оставались в системе.
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	go func() {
		sig := <-signals
		// Код 0: остановка по просьбе — не отказ. С ненулевым кодом (а тем
		// более с гибелью от сигнала) приложение заносит каждый свой выход в
		// журнал как падение ядра.
		shutdown(0, fmt.Sprintln("got signal:", sig))
	}()

	boxmain.DisableColor()

	var conn net.Conn
	var err error
	for i := 0; i < 10; i++ {
		conn, err = ipc.ConnectIPC(socketName, parentcheck.ParentPID)
		if err == nil {
			break
		}
		log.Printf("IPC connect attempt %d/10 failed: %v", i+1, err)
		time.Sleep(500 * time.Millisecond)
	}
	if err != nil {
		log.Fatalf("failed to connect to GUI socket after 10 attempts: %v", err)
	}

	fmt.Println("Core Has Successfully Connected to Throne!")
	rpc.Serve(conn, debug)
	sysdns.Release()
	log.Fatal("IPC connection dropped, exiting")
}

// Выход с откатом. log.Fatalln звал os.Exit, а тот не выполняет ни отложенных
// вызовов, ни чего-либо ещё: туннель, его маршруты, подменённый системный DNS и
// дочерние процессы оставались в системе. Человек при этом остаётся без сети и
// связать это с давно закрытым клиентом не может.
func shutdown(code int, reason string) {
	log.Println("shutting down:", strings.TrimSpace(reason))
	done := make(chan struct{})
	go func() {
		defer close(done)
		if err := rpc.Teardown(context.Background()); err != nil {
			log.Println("teardown failed:", err)
		}
	}()
	// Ограничение по времени: зависший откат не должен держать ядро вечно,
	// иначе вместо следа в системе получим вечный процесс.
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		log.Println("teardown timed out")
	}
	os.Exit(code)
}

func printVersions() {
	fmt.Println("sing-box:", C.Version)
	fmt.Println("Xray-core:", core.Version())
	fmt.Println("Go:", runtime.Version())}

func main() {
	// Before anything prints: stdout is the guard's protocol channel and the --check line.
	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "--guard":
			parentcheck.CheckParentProcess()
			guard.Run(parentcheck.ParentPID)
		case "--check":
			parentcheck.CheckParentProcess()
			if err := capcheck.Print(os.Stdout); err != nil {
				fmt.Fprintf(os.Stderr, "check: %v\n", err)
				os.Exit(1)
			}
			os.Exit(0)
		case "--restore-dns":
			parentcheck.CheckParentProcess()
			sysdns.RestoreStale()
			os.Exit(0)
		case "--version":
			printVersions()
			os.Exit(0)
		}
	}

	defer func() {
		if err := recover(); err != nil {
			// The exit code is all the GUI has to tell a panic from a clean stop.
			fmt.Fprintf(os.Stderr, "Core panicked: %v\n%s\n", err, runtimeDebug.Stack())
			os.Exit(2)
		}
	}()
	printVersions()
	fmt.Println()
	runtimeDebug.SetMemoryLimit(memoryLimit)
	go watchMemory()

	RunCore()
	return
}
