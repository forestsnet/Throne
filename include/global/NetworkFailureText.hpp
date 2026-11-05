#pragma once

#include <QNetworkReply>
#include <QString>

namespace Configs_network {
    // Why a network request failed, in the interface language and without the URL.
    // QNetworkReply::errorString() can do neither: it is English whatever the
    // language is, because Qt's own translations are not in the build - the Windows
    // package has no translations directory, and deploy_linux64.sh removes the ones
    // linuxdeploy brings - and it spells out the whole address, subscription token
    // and all.
    //
    // The text follows a colon in most of its callers ("Could not download the
    // subscription: %1"), so it starts lower case and carries no full stop. Pass 0
    // for httpStatus when no answer arrived, and the reason is read from the error
    // instead.
    QString NetworkFailureText(QNetworkReply::NetworkError error, int httpStatus);
} // namespace Configs_network
