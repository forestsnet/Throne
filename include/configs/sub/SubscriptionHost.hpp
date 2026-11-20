#pragma once

#include <QString>

#include <optional>
#include <QStringList>

namespace Subscription {
    // Каким путём отправить запрос подписки.
    enum class FetchWay {
        AsConfigured,   // как решают настройки клиента
        ThroughCore,    // через ядро запущенного профиля
        Bypass,         // мимо ядра, даже если настройки велят иначе
    };

    // Путь для второй попытки после неудачной первой. Пусто, когда второго
    // пути нет: под TUN туннель забирает и прямой запрос тоже, и повтор был бы
    // тем же самым запросом, а без запущенного профиля идти через ядро некуда.
    std::optional<FetchWay> RetryWay(bool firstWentThroughCore, bool coreReachable, bool tunMode);

    // Подмена домена подписки: панель называет новый хост в заголовке ответа, а
    // путь и query остаются как были — там лежит токен подписчика, и переносить
    // его целиком незачем.
    //
    // Заголовкам верим только на https. На plain http любой на пути вписывает
    // свой хост и забирает подписку себе навсегда, а переезд — вещь постоянная.

    // Хост с портом, если он задан: для лога и уведомления. Пусто, если URL не разбирается.
    QString SubscriptionHost(const QString &url);

    // url с заменённым хостом. Порт меняется только если его назвал candidate:
    // переезд на другой домен того же провайдера порт обычно не трогает.
    // Пусто, когда идти по candidate нельзя: подписка не на https, candidate не
    // «хост» и не «хост:порт», или это тот же адрес, что уже в url.
    QString SwapSubscriptionHost(const QString &url, const QString &candidate);

    // Хосты из x-fsnt-sub-mirrors: через запятую, точку с запятой, пробел или
    // перевод строки. Повторы и неразбираемое отбрасываются, порядок сохраняется.
    QStringList ParseMirrorHosts(const QString &raw);

    // Адреса, которые стоит попробовать, когда сам url не ответил, в порядке зеркал.
    QStringList MirrorCandidates(const QString &url, const QString &mirrors);
}
