#include "include/global/SystemStateJournal.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>

#include <QLoggingCategory>

namespace Configs {
    namespace {
        QMutex g_mutex;
        QString g_directory;

        // Прочитанное на старте. Журнал перечитывается с диска при каждой
        // записи, но то, что оставил прошлый сеанс, нужно запомнить до того,
        // как текущий начнёт его переписывать.
        QList<SystemChange> g_leftovers;

        QList<SystemChange> readFile() {
            const auto path = SystemStateJournal::Path();
            if (path.isEmpty()) return {};
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) return {};
            const auto document = QJsonDocument::fromJson(file.readAll());
            QList<SystemChange> changes;
            for (const auto &value : document.array()) {
                const auto object = value.toObject();
                const auto kind = object.value("kind").toString();
                if (kind.isEmpty()) continue;
                changes.append({kind, object.value("detail").toString()});
            }
            return changes;
        }
    } // namespace

    void SystemStateJournal::Init(const QString &directory) {
        QMutexLocker lock(&g_mutex);
        g_directory = directory;
        g_leftovers = readFile();
    }

    QString SystemStateJournal::Path() {
        if (g_directory.isEmpty()) return {};
        return QDir(g_directory).filePath(QStringLiteral("system_state.json"));
    }

    void SystemStateJournal::write(const QList<SystemChange> &changes) {
        const auto path = Path();
        if (path.isEmpty()) {
            qWarning() << "system state journal is used before Init()";
            return;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "system state journal is not writable: " + file.errorString();
            return;
        }
        QJsonArray array;
        for (const auto &change : changes) {
            array.append(QJsonObject{{"kind", change.kind}, {"detail", change.detail}});
        }
        file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
        // commit() переименовывает готовый файл поверх прежнего: оборваться на
        // середине записи и оставить мусор вместо журнала так нельзя.
        if (!file.commit()) qWarning() << "system state journal was not saved: " + file.errorString();
    }

    void SystemStateJournal::Record(const QString &kind, const QString &detail) {
        QMutexLocker lock(&g_mutex);
        auto changes = readFile();
        for (auto &change : changes) {
            if (change.kind != kind) continue;
            change.detail = detail;
            write(changes);
            return;
        }
        changes.append({kind, detail});
        write(changes);
    }

    void SystemStateJournal::Forget(const QString &kind) {
        QMutexLocker lock(&g_mutex);
        auto changes = readFile();
        const auto before = changes.size();
        changes.removeIf([&kind](const SystemChange &change) { return change.kind == kind; });
        if (changes.size() != before) write(changes);
    }

    QList<SystemChange> SystemStateJournal::Leftovers() {
        QMutexLocker lock(&g_mutex);
        return g_leftovers;
    }
} // namespace Configs
