#include "include/sys/macos/CorePrivilege.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>

#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"
#include "include/sys/macos/MacOS.h"

namespace {
    // Из того же набора, что и moe.Throne.headless: имя службы человек увидит
    // в launchctl, и родство должно быть заметно.
    QString serviceLabel() { return QStringLiteral("moe.Throne.coreprivilege"); }

    QString plistPath() { return QStringLiteral("/Library/LaunchDaemons/%1.plist").arg(serviceLabel()); }

    // Своё поле версии в plist: launchd его не читает, а нам оно говорит, что
    // установленная служба собрана этой же сборкой. Иначе исправление в самом
    // plist никогда бы не доехало до тех, у кого служба уже стоит — путь к
    // ядру совпадает, и переставлять её было бы незачем. Смена версии стоит
    // ровно один запрос пароля, и только когда меняется сама служба.
    constexpr int kServiceVersion = 2;

    QString versionKey() { return QStringLiteral("FsntServiceVersion"); }

    QString shellQuoted(const QString &text) {
        return QStringLiteral("'%1'").arg(QString(text).replace('\'', QStringLiteral("'\\''")));
    }

    QString xmlEscaped(const QString &text) {
        QString out = text;
        out.replace('&', QStringLiteral("&amp;"));
        out.replace('<', QStringLiteral("&lt;"));
        out.replace('>', QStringLiteral("&gt;"));
        return out;
    }

    // Путь ядра лежит в plist отдельной строкой и приходит в оболочку как
    // "$1": внутри самой команды его не надо ни кавычить, ни экранировать, и
    // пробелы в «FSNT Client.app» ничего не ломают.
    //
    // Следим и за файлом, и за каталогом: обновление файл не правит, а
    // подменяет, и наблюдение за одним путём такую подмену пропускает.
    QString plistFor(const QString &corePath) {
        const auto core = xmlEscaped(corePath);
        const auto dir = xmlEscaped(QFileInfo(corePath).absolutePath());
        return QStringLiteral(
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                   "<plist version=\"1.0\"><dict>\n"
                   "  <key>Label</key><string>%1</string>\n"
                   "  <key>ProgramArguments</key><array>\n"
                   "    <string>/bin/sh</string>\n"
                   "    <string>-c</string>\n"
                   "    <string>/usr/sbin/chown root:wheel \"$1\" &amp;&amp; /bin/chmod u+s \"$1\"</string>\n"
                   "    <string>sh</string>\n"
                   "    <string>%2</string>\n"
                   "  </array>\n"
                   "  <key>WatchPaths</key><array>\n"
                   "    <string>%3</string>\n"
                   "    <string>%4</string>\n"
                   "  </array>\n"
                   "  <key>RunAtLoad</key><true/>\n"
                   // Без этого launchd держит между запусками свои 10 секунд.
                   // Обновление пишет в каталог не один раз, первая же запись
                   // запускает работу впустую, и к моменту, когда файл встал
                   // на место, повтор упирается в этот порог.
                   "  <key>ThrottleInterval</key><integer>1</integer>\n"
                   "  <key>%5</key><integer>%6</integer>\n"
                   "</dict></plist>\n")
            // Каждый номер встречается один раз: повторённый %2 читается
            // по-разному у одноаргументного arg() и у многоаргументного.
            .arg(serviceLabel(), core, core, dir)
            .arg(versionKey())
            .arg(kServiceVersion);
    }

    struct InstalledService {
        QString core;
        int version = 0;
    };

    // Что установлено. core пуст, если службы нет.
    InstalledService installedService() {
        if (!QFileInfo::exists(plistPath())) return {};
        QProcess plutil;
        plutil.start(QStringLiteral("/usr/bin/plutil"),
                     {QStringLiteral("-convert"), QStringLiteral("json"), QStringLiteral("-o"), QStringLiteral("-"), plistPath()});
        if (!plutil.waitForFinished(5000) || plutil.exitCode() != 0) return {};
        const auto root = QJsonDocument::fromJson(plutil.readAllStandardOutput()).object();
        // Аргументы: /bin/sh, -c, команда, sh, путь ядра.
        const auto args = root.value(QStringLiteral("ProgramArguments")).toArray();
        if (args.size() < 5) return {};
        return {args.at(4).toString(), root.value(versionKey()).toInt()};
    }

    // Одна цепочка под одной авторизацией. plist уезжает в base64: иначе его
    // пришлось бы протаскивать через кавычки оболочки внутри кавычек
    // AppleScript, и любой апостроф в пути ломал бы всю команду.
    //
    // bootout до bootstrap: без него повторная установка падает на том, что
    // служба с таким именем уже загружена. Её отсутствие — не ошибка.
    QString installCommand(const QString &corePath) {
        const auto payload = QString::fromLatin1(plistFor(corePath).toUtf8().toBase64());
        const auto plist = shellQuoted(plistPath());
        const auto core = shellQuoted(corePath);
        return QStringLiteral("/bin/mkdir -p /Library/LaunchDaemons && "
                              "printf %s '%1' | /usr/bin/base64 -D -o %2 && "
                              "/usr/sbin/chown root:wheel %2 && /bin/chmod 644 %2 && "
                              "{ /bin/launchctl bootout system/%3 >/dev/null 2>&1 || true ; } && "
                              "/bin/launchctl bootstrap system %2 && "
                              "/usr/sbin/chown root:wheel %4 && /bin/chmod u+s %4")
            .arg(payload, plist, serviceLabel(), core);
    }

    QString removeCommand(const QString &corePath) {
        QString command = QStringLiteral("{ /bin/launchctl bootout system/%1 >/dev/null 2>&1 || true ; } && /bin/rm -f %2")
                              .arg(serviceLabel(), shellQuoted(plistPath()));
        if (!corePath.isEmpty()) {
            command += QStringLiteral(" && /bin/chmod u-s %1").arg(shellQuoted(corePath));
        }
        return command;
    }

    bool runAuthorized(const QString &command, const QString &prompt, int *code) {
        const auto ret = Mac_Run_Command(command, prompt);
        if (code != nullptr) *code = ret;
        return ret == 0;
    }
} // namespace

namespace Sys::MacCorePrivilege {
    bool Installed() { return !installedService().core.isEmpty(); }

    bool WatchesCore(const QString &corePath) {
        const auto installed = installedService();
        if (installed.core.isEmpty() || corePath.isEmpty()) return false;
        if (installed.version != kServiceVersion) return false;
        return QFileInfo(installed.core).absoluteFilePath() == QFileInfo(corePath).absoluteFilePath();
    }

    bool Install(const QString &corePath, const QString &prompt, int *code) {
        if (corePath.isEmpty()) return false;

        if (!runAuthorized(installCommand(corePath), prompt, code)) return false;

        // По итогу, а не по коду возврата: до конца цепочки могло и не дойти.
        if (!WatchesCore(corePath)) {
            LOG_WARN("the core privilege service was not installed");
            return false;
        }
        if (!Configs::isSetuidSet(corePath.toStdString())) {
            LOG_WARN("the core privilege service is installed but the core did not become setuid");
            return false;
        }
        LOG_INFO("the core privilege service now watches " + corePath);
        return true;
    }

    bool Reapply(const QString &corePath, int timeoutMs) {
        if (!WatchesCore(corePath)) return false;
        if (Configs::isSetuidSet(corePath.toStdString())) return true;

        // launchd слушает каталог ядра, и любое изменение в нём поднимает
        // работу. Замер на живой службе: отклик за 2–3 секунды.
        const auto nudge = QDir(QFileInfo(corePath).absolutePath()).filePath(QStringLiteral(".fsnt-privilege-nudge"));
        QFile marker(nudge);
        if (marker.open(QIODevice::WriteOnly)) {
            marker.close();
            QFile::remove(nudge);
        } else {
            LOG_WARN("could not nudge the tunnel permission service: " + nudge + " is not writable");
        }

        for (int waited = 0; waited < timeoutMs; waited += 100) {
            if (Configs::isSetuidSet(corePath.toStdString())) return true;
            QThread::msleep(100);
        }
        return Configs::isSetuidSet(corePath.toStdString());
    }

    bool Remove(const QString &corePath, const QString &prompt, int *code) {
        if (!runAuthorized(removeCommand(corePath), prompt, code)) return false;
        if (Installed()) {
            LOG_WARN("the core privilege service is still installed");
            return false;
        }
        LOG_INFO("the core privilege service was removed");
        return true;
    }
} // namespace Sys::MacCorePrivilege
