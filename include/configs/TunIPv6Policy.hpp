#pragma once

#include <QJsonObject>
#include <QString>

namespace Configs {
    // Apply after routing and DNS are built, including user-supplied sections.
    void ApplyTunIPv6Policy(QJsonObject &config, const QString &tunTag, const QString &systemDnsTag);
}
