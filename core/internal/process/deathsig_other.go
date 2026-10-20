//go:build !linux

package process

import "os/exec"

// В macOS и Windows аналога Pdeathsig нет. Там сироту не допускает сам порядок
// завершения: ядро сворачивается по сигналу и по уходу родителя, а Stop убивает
// дочерний процесс до выхода.
func applyDeathSignal(cmd *exec.Cmd) {}
