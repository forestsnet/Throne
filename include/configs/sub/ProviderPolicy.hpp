#pragma once

#include <optional>

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

namespace Subscription {
    // Настройки, присланные панелью в HTTP-заголовках ответа подписки.
    // Трёхзначность обязательна: отсутствие заголовка не равно false.
    struct ProviderPolicy {
        int schema = 1;

        // Информационные
        QString title;                    // profile-title
        QString announce;                 // announce
        QString supportUrl;               // support-url
        QString webPageUrl;               // profile-web-page-url
        QString providerId;               // providerid
        qint64  refillDate = 0;           // subscription-refill-date
        int     updateIntervalHours = 0;  // profile-update-interval, 0 = не прислан

        // Управляющие
        std::optional<bool> tunEnable;    // tun-enable
        std::optional<bool> alwaysHwid;   // subscription-always-hwid-enable
        std::optional<bool> autoUpdate;   // subscription-auto-update-enable
        std::optional<bool> dnsFromJson;  // dns-from-json-enable
        std::optional<bool> hideSettings; // hide-settings
        std::optional<bool> hideUrl;      // hide-url
        std::optional<bool> pin;          // subscription-pin
        std::optional<bool> collapse;     // subscriptions-collapse
        std::optional<bool> pingOnOpen;   // subscription-ping-onopen-enabled

        QString perAppProxyList;          // per-app-proxy-list, формат не разбираем
        QJsonObject unknown;              // нераспознанные заголовки, чтобы не терять

        bool isEmpty() const;
    };

    ProviderPolicy ParseProviderPolicy(const QList<QPair<QByteArray, QByteArray>> &headers);
    QString        SerializeProviderPolicy(const ProviderPolicy &policy);
    ProviderPolicy DeserializeProviderPolicy(const QString &json);

    // Политика группы, которой принадлежит запущенный профиль.
    // Пустая, если профиль не запущен: остановка профиля снимает все ограничения.
    const ProviderPolicy &ActiveProviderPolicy();
    // Группа, чья политика сейчас активна; -1 если активной политики нет.
    int ActiveProviderPolicyGroup();
    void SetActiveProviderPolicy(const ProviderPolicy &policy, int gid);
    void ClearActiveProviderPolicy();

    // Запросы ограничений. Общее правило: отсутствие заголовка — ограничение снято,
    // поэтому остановка профиля всегда возвращает пользователю полный интерфейс.
    bool PolicyHidesSettings();
    bool PolicyHidesUrl();
    bool PolicyBlocksDeletion(int gid);
}
