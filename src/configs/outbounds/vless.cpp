#include "include/configs/outbounds/vless.h"

#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
    bool vless::ParseFromLink(const QString& link)
    {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query(QUrl::ComponentFormattingOption::FullyDecoded));

        outbound::ParseFromLink(link);
        uuid = url.userName();
        if (server_port == 0) server_port = 443;

        flow = GetQueryValue(query, "flow", "");

        transport->ParseFromLink(link);
        
        tls->ParseFromLink(link);
        if (!tls->server_name.isEmpty()) {
            tls->enabled = true;
        }
        
        packet_encoding = GetQueryValue(query, "packetEncoding", "xudp");
        multiplex->ParseFromLink(link);

        return !(uuid.isEmpty() || server.isEmpty());
    }

    bool vless::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty() || object["type"].toString() != "vless") return false;
        outbound::ParseFromJson(object);
        if (object.contains("uuid")) uuid = object["uuid"].toString();
        if (object.contains("flow")) flow = object["flow"].toString();
        if (object.contains("packet_encoding")) packet_encoding = object["packet_encoding"].toString();
        if (object.contains("tls")) tls->ParseFromJson(object["tls"].toObject());
        if (object.contains("transport")) transport->ParseFromJson(object["transport"].toObject());
        if (object.contains("multiplex")) multiplex->ParseFromJson(object["multiplex"].toObject());
        return true;
    }

    QString vless::ExportToLink()
    {
        QUrl url;
        QUrlQuery query;
        url.setScheme("vless");
        url.setUserName(uuid);
        url.setHost(server);
        url.setPort(server_port);
        if (!name.isEmpty()) url.setFragment(name);

        query.addQueryItem("encryption", "none");
        if (!flow.isEmpty()) query.addQueryItem("flow", flow);

        mergeUrlQuery(query, tls->ExportToLink());
        mergeUrlQuery(query, transport->ExportToLink());
        mergeUrlQuery(query, multiplex->ExportToLink());
        
        if (!packet_encoding.isEmpty()) query.addQueryItem("packetEncoding", packet_encoding);
        
        if (!query.isEmpty()) url.setQuery(query);
        return url.toString(QUrl::FullyEncoded);
    }

    QJsonObject vless::ExportToJson()
    {
        QJsonObject object;
        object["type"] = "vless";
        mergeJsonObjects(object, outbound::ExportToJson());
        if (!uuid.isEmpty()) object["uuid"] = uuid;
        if (!flow.isEmpty()) object["flow"] = flow;
        if (!packet_encoding.isEmpty()) object["packet_encoding"] = packet_encoding;
        if (tls->enabled) object["tls"] = tls->ExportToJson();
        if (!transport->type.isEmpty()) object["transport"] = transport->ExportToJson();
        if (multiplex->enabled) object["multiplex"] = multiplex->ExportToJson();
        return object;
    }

    BuildResult vless::Build()
    {
        QJsonObject object;
        object["type"] = "vless";
        mergeJsonObjects(object, outbound::Build().object);
        if (!uuid.isEmpty()) object["uuid"] = uuid;
        if (!flow.isEmpty()) object["flow"] = flow;
        if (!packet_encoding.isEmpty()) object["packet_encoding"] = packet_encoding;
        if (tls->enabled) object["tls"] = tls->Build().object;
        if (!transport->type.isEmpty()) object["transport"] = transport->Build().object;
        if (auto obj = multiplex->Build().object; !obj.isEmpty()) object["multiplex"] = obj;
        return {object, ""};
    }

    QString vless::DisplayType()
    {
        return "VLESS";
    }
}
