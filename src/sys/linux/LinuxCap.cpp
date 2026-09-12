#include "include/sys/linux/LinuxCap.h"

#include <QDebug>
#include <QProcess>
#include <QStandardPaths>

int Linux_Run_Command(const QString &commandName, const QStringList &args) {
    // Аргументами, а не строкой для оболочки: путь к ядру может содержать
    // пробелы (домашний каталог, имя пользователя), и склеенная команда
    // разъезжалась — pkexec получал половину пути и молча ничего не делал.
    QProcess pkexec;
    pkexec.start(QStringLiteral("pkexec"), QStringList{Linux_FindCapProgsExec(commandName)} + args);
    if (!pkexec.waitForStarted(5000)) return -1;
    // Ждём столько, сколько человек будет вводить пароль в окне polkit.
    pkexec.waitForFinished(-1);
    return pkexec.exitCode();
}

bool Linux_HavePkexec() {
    QProcess p;
    p.setProgram("pkexec");
    p.setArguments({"--help"});
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start();
    p.waitForFinished(500);
    return (p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1) == 0;
}

QString Linux_FindCapProgsExec(const QString &name) {
    QString exec = QStandardPaths::findExecutable(name);
    if (exec.isEmpty())
        exec = QStandardPaths::findExecutable(name, {"/usr/sbin", "/sbin"});

    if (exec.isEmpty())
        qDebug() << "Executable" << name << "could not be resolved";
    else
        qDebug() << "Found exec" << name << "at" << exec;

    return exec.isEmpty() ? name : exec;
}
