// iCloud Private Relay не уступает дорогу нашему туннелю. utun, поднятый
// напрямую, система за VPN не считает — для этого нужен NetworkExtension, а он
// требует подписи и прав, которых у сборки нет, — и Safari продолжает ходить
// через Apple: человек выбрал страну, а сайты видят реле.
//
// Запрет mask.icloud.com и mask-h2.icloud.com по DNS закрывает только настройку
// нового сеанса. Адреса входных узлов устройство берёт из конфигурации,
// полученной раньше, и идёт к ним по IP — мимо любых правил по именам. Вдобавок
// реле шифрует SNI (ECH), поэтому отбить соединение по имени на маршруте тоже
// нельзя.
//
// Поэтому дёргаем тот же выключатель, что и System Settings: «Limit IP Address
// Tracking» у активного интерфейса, то есть ключ DisablePrivateRelay в
// настройках SystemConfiguration. Прежнее значение сохраняем на диске: не дожил
// сеанс до Restore — вернём при следующем запуске.
package sysrelay

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"strings"

	tun "github.com/sagernet/sing-tun"
)

const (
	relayKey = "DisablePrivateRelay"
	// Рядом с cache.db: рабочий каталог ядра — его каталог настроек.
	markerName = "private-relay.restore"
	// Ключа у интерфейса не было вовсе, и вернуть всё как было — значит убрать его снова.
	valueAbsent = "-"
)

// scutil --prefs читает команды со стандартного ввода и работает теми же
// вызовами SCPreferences, какими правит настройки сама система.
func prefs(commands ...string) (string, error) {
	command := exec.Command("/usr/sbin/scutil", "--prefs")
	command.Stdin = strings.NewReader(strings.Join(append(commands, "quit", ""), "\n"))
	output, err := command.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("scutil --prefs: %w: %s", err, strings.TrimSpace(string(output)))
	}
	return string(output), nil
}

// currentSet — путь набора сетевых настроек, внутри которого лежат интерфейсы.
// У каждого Mac он свой и меняется вместе с «расположением» (Location).
func currentSet() (string, error) {
	out, err := prefs("d.init", "get /", "d.show")
	if err != nil {
		return "", err
	}
	if set := value(out, "CurrentSet"); set != "" {
		return set, nil
	}
	return "", fmt.Errorf("the SystemConfiguration preferences name no CurrentSet")
}

// value достаёт «ключ : значение» из того, что напечатал d.show.
func value(output, key string) string {
	for _, line := range strings.Split(output, "\n") {
		name, found, ok := strings.Cut(line, ":")
		if ok && strings.TrimSpace(name) == key {
			return strings.TrimSpace(found)
		}
	}
	return ""
}

func interfacePath(set, interfaceName string) string {
	return set + "/Network/Interface/" + interfaceName
}

// read отдаёт valueAbsent, если ключа у интерфейса нет.
func read(path string) (string, error) {
	// d.init перед get обязателен: get по отсутствующему пути словарь не
	// трогает, и там осталось бы содержимое от предыдущей команды.
	out, err := prefs("d.init", "get "+path, "d.show")
	if err != nil {
		return "", err
	}
	if current := value(out, relayKey); current != "" {
		return current, nil
	}
	return valueAbsent, nil
}

// write ставит значение или убирает ключ, не задев остальные ключи интерфейса —
// там же живёт, например, EnableLowDataMode.
func write(path, want string) error {
	change := "d.add " + relayKey + " # " + want
	if want == valueAbsent {
		change = "d.remove " + relayKey
	}
	if _, err := prefs("d.init", "get "+path, change, "set "+path, "commit", "apply"); err != nil {
		return err
	}
	// По итогу, а не по коду возврата: без root commit не проходит, а scutil
	// сообщает об этом строкой и выходит с нулём.
	got, err := read(path)
	if err != nil {
		return err
	}
	if got != want {
		return fmt.Errorf("%s of %s stayed %s instead of %s", relayKey, path, got, want)
	}
	return nil
}

// Disable выключает реле на интерфейсе, через который сейчас идёт трафик.
func Disable(interfaceMonitor tun.DefaultInterfaceMonitor) error {
	interfaceName := interfaceMonitor.DefaultInterface().Name
	if interfaceName == "" {
		return fmt.Errorf("the interface monitor names no default interface")
	}
	set, err := currentSet()
	if err != nil {
		return err
	}
	path := interfacePath(set, interfaceName)
	previous, err := read(path)
	if err != nil {
		return err
	}
	if previous == "1" {
		// Человек выключил реле на этой сети сам: возвращать потом нечего.
		return nil
	}
	// Метка — до изменения: иначе убитое ядро не оставит следов, и выключатель
	// останется в нашем положении навсегда.
	if err := os.WriteFile(markerName, []byte(interfaceName+" "+previous+"\n"), 0o644); err != nil {
		return err
	}
	if err := write(path, "1"); err != nil {
		os.Remove(markerName)
		return err
	}
	log.Println("iCloud Private Relay switched off on", interfaceName)
	return nil
}

// Restore возвращает выключатель в то положение, в котором его застали.
func Restore() error {
	marker, err := os.ReadFile(markerName)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return err
	}
	fields := strings.Fields(string(marker))
	if len(fields) != 2 {
		os.Remove(markerName)
		return fmt.Errorf("%s holds %q, want an interface and a value", markerName, strings.TrimSpace(string(marker)))
	}
	set, err := currentSet()
	if err != nil {
		return err
	}
	if err := write(interfacePath(set, fields[0]), fields[1]); err != nil {
		return err
	}
	log.Println("iCloud Private Relay restored on", fields[0])
	return os.Remove(markerName)
}

// RepairLeftover — для оборвавшегося сеанса: до Restore ядро, убитое SIGKILL,
// не доходит, и реле остаётся выключенным. Чиним при запуске.
func RepairLeftover() {
	if _, err := os.Stat(markerName); err != nil {
		return
	}
	if err := Restore(); err != nil {
		log.Println("iCloud Private Relay was left off by the previous session and could not be restored:", err)
	}
}
