#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Решения проверки страны, которым не нужна сеть: в каком порядке спрашивать
// провайдеров, что лежит в ответе DoH и как сходить по адресу вместо имени.
// Отдельно от ExitLookup.cpp, потому что проверять их надо без сети.
namespace Configs_network {
    // Порядок опроса. «Автоматически» ставит проверку по адресу второй: ей
    // не нужно имя, и она отвечает, когда резолвер молчит. Выбранного
    // человеком провайдера спрашиваем его одного: он выбран ради своих данных,
    // а имя ему, если ядро не смогло, разрешит вызывающий. known —
    // идентификаторы, которые знает сборка.
    QStringList ExitLookupChain(const QString &providerId, const QStringList &known);

    // Адреса из ответа DoH в виде JSON: Cloudflare и Google отдают один вид.
    // Берутся только записи A, в порядке ответа и без повторов.
    QStringList ParseDohAddresses(const QByteArray &json);

    // Тот же URL с адресом вместо имени. Имя остаётся вызывающему: его место в
    // заголовке Host и в проверке сертификата. Пусто, если URL без имени или
    // адрес не разбирается.
    QString UrlWithAddress(const QString &url, const QString &address);
} // namespace Configs_network
