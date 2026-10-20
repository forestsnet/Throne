#pragma once

#include <QJsonObject>

namespace Configs {
    // Сколько частей конвейера DNS было убрано — для лога.
    struct XrayDnsStripResult {
        int outbounds = 0;
        int rules = 0;
    };

    // Убирает из конфига Xray весь конвейер DNS, а не только список серверов.
    //
    // DNS в конфиге состоит из трёх связанных частей: блока dns, исходящего с
    // protocol "dns" и правил маршрутизации, гонящих в него запросы. Если убрать
    // только блок dns, исходящий и правила остаются, и Xray на каждом резолве
    // отвечает «failed to resolve ip > dns router closed»: правило шлёт запрос
    // в модуль, которого уже нет.
    XrayDnsStripResult StripXrayDns(QJsonObject &config);
}
