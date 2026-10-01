#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Configs {
    // Один резолвер со всеми своими транспортами. Держать их вместе важно:
    // когда у человека режут 853, провайдер не должен выпадать целиком —
    // тот же сервер прекрасно отвечает по UDP.
    struct DnsCatalogEntry {
        QString title;
        QStringList addresses; // в порядке предпочтения
        bool forDirect = true;
        bool forRemote = true;

        [[nodiscard]] bool isSystem() const {
            return addresses.size() == 1 && addresses.first() == QLatin1String("local");
        }
    };

    // Встроенный список, перекрытый файлом рядом с настройками, если он есть.
    QList<DnsCatalogEntry> DnsCatalog();

    // Путь к перекрывающему файлу — его же показываем человеку, когда он
    // хочет править список руками.
    QString DnsCatalogPath();

    // Записывает свой список. error заполняется, если JSON не разобрался.
    bool SaveDnsCatalog(const QString &json, QString *error);

    // Порядок, в котором подбор пробует серверы: сначала то, что задано, потом
    // тот же сервер другими транспортами, потом свои адреса человека, потом
    // каталог. Системный резолвер всегда последний: он работает почти всегда,
    // и поставь его раньше — подбор остановится на нём и не доберётся до
    // серверов, которые человек выбрал осознанно.
    QStringList DnsCandidates(const QString &configured, bool direct);
} // namespace Configs
