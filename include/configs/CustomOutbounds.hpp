#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <optional>

namespace Configs
{
    // Чтение списка исходящих из конфига, вставленного человеком целиком.
    //
    // Xray сохраняет одно написание ключа, а конфиг может прийти с любым, и
    // спорящие написания — это не значение, а неизвестность. На этом разборе
    // стоят адрес профиля, его SNI и перечисление узлов, а проверить его можно
    // только тестом — поэтому он лежит отдельно от выводов, которые из него
    // делают. Имена низкоуровневых разборщиков оставлены как были: их зовут
    // несколько десятков мест в custom.cpp, и переименование закопало бы
    // перенос в шум.

    // Ключи объекта, совпадающие с name без учёта регистра.
    QStringList keysNamed(const QJsonObject& o, const QString& name);

    // Значение, на котором сошлись все написания ключа. Undefined, если они
    // спорят или ключа нет вовсе.
    QJsonValue xrayAgreed(const QJsonObject& o, const QString& name);

    // Для имён, которые Xray приводит к нижнему регистру. Пусто — их нет
    // вовсе, nullopt — написания спорят.
    std::optional<QString> xrayAgreedName(const QStringList& lowerCased);

    std::optional<QString> xrayAgreedProtocol(const QJsonObject& o);

    // Xray позволяет непустому адресу верхнего уровня заменить vnext и servers,
    // забирая с собой встроенного пользователя. nullopt — написания спорят.
    std::optional<bool> xrayInlinePeer(const QJsonObject& settings);

    // vnext читают только VMess и VLESS, остальные протоколы читают servers.
    QJsonObject xrayPeer(const QJsonObject& settings, const QString& protocol);

    // Узел исходящего. Пусто, если протокола нет или его написания спорят: тогда
    // неизвестно даже, какой список читает Xray.
    QJsonObject xrayOutboundPeer(const QJsonObject& o);
    QString xrayOutboundAddress(const QJsonObject& o);

    // Протоколы и типы, которые никуда не звонят: их адрес — не адрес.
    bool isXrayInfra(const QString& protocol);
    bool isSingBoxInfra(const QString& type);

    // Первый исходящий, который куда-то звонит: туда Xray маршрутизирует по
    // умолчанию, и оттуда берутся адрес профиля и его SNI.
    QJsonObject firstXrayEgress(const QJsonArray& outbounds);

    // Имена всех узлов конфига, по порядку и без повторов. Группы выбора своего
    // адреса не имеют, а их участники лежат в том же списке и просматриваются
    // сами. Балансировщик провайдера держит по узлу на страну, и разрешать перед
    // подключением надо каждое имя, а не только первое.
    QStringList XrayConfigServerNames(const QJsonObject& root);
    QStringList SingBoxConfigServerNames(const QJsonObject& root);
} // namespace Configs
