#pragma once

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>

namespace Subscription {
    // Jobs run one at a time on a background worker, in FIFO order.
    class GroupUpdater : public QObject {
        Q_OBJECT

    public:
        using Finish = std::function<void()>;

        // showDiff: a manual refresh pops up the diff; automatic paths leave it false and only log.
        void RefreshGroup(int gid, const Finish &finish = nullptr, bool showDiff = false);

        void RefreshAll(bool onlyAllowed = false);

        // Результат подписки по ссылке: удалась ли загрузка и была ли эта
        // ссылка уже добавлена раньше. Диалогу это нужно, чтобы сказать
        // человеку правду, а не молча закрыться.
        enum class SubscribeResult { Added, Updated, Failed };
        using SubscribeFinish = std::function<void(SubscribeResult)>;
        void SubscribeUrl(const QString &url, const SubscribeFinish &finish = nullptr);

        void ImportUrl(const QString &url, const Finish &finish = nullptr);

        void ImportText(const QString &text, int gid = -1, const Finish &finish = nullptr);

        void ImportBatch(const QStringList &payloads, const Finish &finish = nullptr);

    signals:
        void asyncUpdateCallback(int gid);

    private:
        struct Job {
            int gid = -1;
            bool batch = false;
            std::function<void()> run;
        };

        void enqueue(Job job);
        void enqueueLocked(Job job);
        void drain();
        void refresh(int gid, bool showDiff);
        void importDocuments(int gid, QList<QByteArray> documents);
        bool fetch(const QString &url, const QString &name, QByteArray &body, QString &userInfo,
                   QString &policyJson);

        QMutex mutex;
        QList<Job> queue;
        QSet<int> pending;
        int pendingBatch = 0;
        bool running = false;
    };

    GroupUpdater *updater();
} // namespace Subscription
