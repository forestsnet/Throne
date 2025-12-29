#include "include/configs/common/Outbound.h"
#include "include/configs/common/utils.h"

namespace Configs {
    bool outbound::ParseFromLink(const QString& link)
    {
        auto url = QUrl(link);
        if (!url.isValid())
        {
            if(!url.errorString().startsWith("Invalid port"))
                return false;
            server_port = 0;
        } else {
            server_port = url.port();
        }

        if (url.hasFragment()) name = url.fragment(QUrl::FullyDecoded);
        server = url.host();
        dialFields->ParseFromLink(link);
        return true;
    }
    bool outbound::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("tag")) name = object["tag"].toString();
        if (object.contains("server")) server = object["server"].toString();
        if (object.contains("server_port")) server_port = object["server_port"].toInt();
        dialFields->ParseFromJson(object);
        return true;
    }
    QString outbound::ExportToLink()
    {
        QUrlQuery query;
        mergeUrlQuery(query, dialFields->ExportToLink());
        return query.toString();
    }
    QJsonObject outbound::ExportToJson()
    {
        QJsonObject object;
        if (!name.isEmpty()) object["tag"] = name;
        if (!server.isEmpty()) object["server"] = server;
        if (server_port > 0) object["server_port"] = server_port;
        auto dialFieldsObj = dialFields->ExportToJson();
        mergeJsonObjects(object, dialFieldsObj);
        return object;
    }
    BuildResult outbound::Build()
    {
        QJsonObject object;
        if (!server.isEmpty()) object["server"] = server;
        if (server_port > 0) object["server_port"] = server_port;
        mergeJsonObjects(object, dialFields->Build().object);
        return {object, ""};
    }
}


