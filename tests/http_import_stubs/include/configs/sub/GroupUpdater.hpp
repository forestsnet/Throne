#pragma once
#include <QString>
#include <QStringList>
#include <functional>
namespace Subscription {
struct ImportReport {
    QStringList reasons;
    QStringList fatalReasons() const { return reasons; }
};
struct UpdateOutcome {
    enum class Failure { None, Fetch };
    Failure failure = Failure::None;
    QString error;
    ImportReport report;
    bool ok() const { return failure == Failure::None; }
};
struct GroupUpdater {
    using OutcomeFinish = std::function<void(const UpdateOutcome &)>;
    OutcomeFinish pending;
    QString requestedUrl;
    bool IsIdle() const { return true; }
    void SubscribeUrlWithOutcome(const QString &url, const OutcomeFinish &done) { requestedUrl = url; pending = done; }
    void ImportTextTo(int, const QString &, const OutcomeFinish &done) { pending = done; }
};
inline GroupUpdater *updater() { static GroupUpdater instance; return &instance; }
}
