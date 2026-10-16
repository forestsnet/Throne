//go:build unix

package process

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"os/user"
	"strconv"
	"strings"
	"syscall"
)

func startChild(path string, args []string, noOut bool) (running, error) {
	cmd := newCmd(path, args, noOut)
	if err := applyPrivilegeDrop(cmd); err != nil {
		return nil, err
	}
	applyDeathSignal(cmd)
	return startCmd(cmd)
}

// Дочерний процесс (openvpn, openconnect и подобные) поднимает собственный
// туннель и собственные маршруты, а убирается только в Stop. Переживи он смерть
// ядра — останется сиротой вместе со своим туннелем, и выключить его человеку
// будет уже нечем. На Linux ядро само пришлёт ему сигнал следом за нами.

// Under setuid-root the real uid still identifies the launching user; SysProcAttr.Credential setuids in the child before exec, race-free.
func applyPrivilegeDrop(cmd *exec.Cmd) error {
	if os.Geteuid() != 0 {
		return nil
	}
	ruid := os.Getuid()
	rgid := os.Getgid()
	if ruid == 0 {
		return errors.New("refusing to start extra process as root: no unprivileged user to drop to")
	}

	cmd.SysProcAttr = &syscall.SysProcAttr{
		Credential: &syscall.Credential{
			Uid:    uint32(ruid),
			Gid:    uint32(rgid),
			Groups: supplementaryGroups(ruid, rgid),
		},
	}
	cmd.Env = userEnv(cmd.Env, ruid)
	return nil
}

func supplementaryGroups(ruid, rgid int) []uint32 {
	fallback := []uint32{uint32(rgid)}
	u, err := user.LookupId(strconv.Itoa(ruid))
	if err != nil {
		return fallback
	}
	ids, err := u.GroupIds()
	if err != nil {
		return fallback
	}
	groups := make([]uint32, 0, len(ids))
	for _, id := range ids {
		if n, err := strconv.Atoi(id); err == nil {
			groups = append(groups, uint32(n))
		}
	}
	if len(groups) == 0 {
		return fallback
	}
	return groups
}

func userEnv(env []string, ruid int) []string {
	u, err := user.LookupId(strconv.Itoa(ruid))
	if err != nil {
		return env
	}
	out := make([]string, 0, len(env)+3)
	for _, kv := range env {
		if name, _, ok := strings.Cut(kv, "="); ok {
			switch name {
			case "HOME", "USER", "LOGNAME":
				continue
			}
		}
		out = append(out, kv)
	}
	return append(out, "HOME="+u.HomeDir, "USER="+u.Username, "LOGNAME="+u.Username)
}

// os.File.Chown/Chmod are fchown/fchmod on the fd, never a path re-resolution, so a swapped symlink cannot redirect them.
func makeConfigReadable(f *os.File) error {
	if os.Geteuid() != 0 {
		return f.Chmod(0o600)
	}
	ruid := os.Getuid()
	rgid := os.Getgid()
	if ruid == 0 {
		// Start() refuses to launch in this case anyway.
		return nil
	}
	if err := f.Chown(ruid, rgid); err != nil {
		return err
	}
	return f.Chmod(0o600)
}

// Каталоги с конфигами удаляются в Stop. Сеанс, не доживший до Stop, оставляет
// их в /tmp во владении root, и за месяцы их набирается заметно. Сносим те,
// чей процесс уже мёртв: чужое работающее ядро при этом не трогаем.
func SweepStaleConfigDirs() {
	entries, err := os.ReadDir("/tmp")
	if err != nil {
		return
	}
	for _, entry := range entries {
		if !entry.IsDir() || !strings.HasPrefix(entry.Name(), "throne-extra-") {
			continue
		}
		rest := strings.TrimPrefix(entry.Name(), "throne-extra-")
		dash := strings.Index(rest, "-")
		if dash <= 0 {
			continue // старое имя без PID: трогать не берёмся
		}
		pid, err := strconv.Atoi(rest[:dash])
		if err != nil || pid == os.Getpid() {
			continue
		}
		if proc, err := os.FindProcess(pid); err == nil && proc.Signal(syscall.Signal(0)) == nil {
			continue // процесс жив, каталог его
		}
		_ = os.RemoveAll("/tmp/" + entry.Name())
	}
}

// When elevated, $TMPDIR is attacker-controlled: use a root-owned 0711 directory in sticky /tmp, which no unprivileged user can swap or list.
func createSecureConfigFile() (*os.File, string, error) {
	if os.Geteuid() != 0 {
		f, err := os.CreateTemp("", "throne-extra-*.conf")
		if err != nil {
			return nil, "", err
		}
		return f, f.Name(), nil
	}

	// PID в имени, чтобы подметание при следующем запуске отличало наш мусор
	// от каталога живого соседнего ядра.
	dir, err := os.MkdirTemp("/tmp", fmt.Sprintf("throne-extra-%d-", os.Getpid()))
	if err != nil {
		return nil, "", err
	}
	// fchmod the directory via its own descriptor (no path re-resolution).
	if d, derr := os.Open(dir); derr == nil {
		_ = d.Chmod(0o711)
		_ = d.Close()
	}
	f, err := os.CreateTemp(dir, "extra-*.conf")
	if err != nil {
		_ = os.RemoveAll(dir)
		return nil, "", err
	}
	return f, dir, nil
}
