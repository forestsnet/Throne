#include "include/configs/outbounds/hysteria.h"

#include <QJsonArray>
#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
    bool hysteria::ParseFromLink(const QString& link)
    {
        auto url = QUrl(link);
        if (!url.isValid())
        {
            if(!url.errorString().startsWith("Invalid port"))
                return false;
            QString authority = url.authority();
            int portStartIndex = link.indexOf(authority) + authority.length();
            QRegularExpressionMatch match = QRegularExpression("^:([\\d,\\-]+)").match(link.mid(portStartIndex));
            if (match.hasMatch()) {
                server_ports = match.captured(1).split(",");
                for (auto & serverPort : server_ports) {
                    serverPort.replace("-", ":");
                }
            }
        }
        auto query = QUrlQuery(url.query(QUrl::ComponentFormattingOption::FullyDecoded));

        outbound::ParseFromLink(link);
        
        if (url.scheme() == "hysteria") {
            protocol_version = "1";
            if (query.hasQueryItem("obfsParam")) obfs = QUrl::fromPercentEncoding(query.queryItemValue("obfsParam").toUtf8());
            if (query.hasQueryItem("auth")) {
                auth = query.queryItemValue("auth");
                auth_type = "STRING";
            }
            if (query.hasQueryItem("recv_window_conn")) recv_window_conn = query.queryItemValue("recv_window_conn").toInt();
            if (query.hasQueryItem("recv_window")) recv_window = query.queryItemValue("recv_window").toInt();
            if (query.hasQueryItem("disable_mtu_discovery")) disable_mtu_discovery = query.queryItemValue("disable_mtu_discovery") == "true";
        } else {
            protocol_version = "2";
            if (url.password().isEmpty()) {
                password = url.userName();
            } else {
                password = url.userName() + ":" + url.password();
            }
            if (query.hasQueryItem("obfs-password")) {
                obfs = QUrl::fromPercentEncoding(query.queryItemValue("obfs-password").toUtf8());
            }
        }
        
        if (query.hasQueryItem("upmbps")) up_mbps = query.queryItemValue("upmbps").toInt();
        if (query.hasQueryItem("downmbps")) down_mbps = query.queryItemValue("downmbps").toInt();
        if (query.hasQueryItem("hop_interval")) hop_interval = query.queryItemValue("hop_interval");
        if (query.hasQueryItem("mport")) {
            server_ports = query.queryItemValue("mport").split(",");
            for (auto & server_port : server_ports) {
                server_port.replace("-", ":");
            }
        }
        
        tls->ParseFromLink(link);
        tls->enabled = true; // Hysteria always uses tls
        
        if (server_port == 0 && server_ports.isEmpty()) server_port = 443;

        return true;
    }

    bool hysteria::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object["type"].toString() == "hysteria") {
            protocol_version = "1";
        } else if (object["type"].toString() == "hysteria2") {
            protocol_version = "2";
        } else {
            return false;
        }
        outbound::ParseFromJson(object);
        if (object.contains("server_ports")) {
            server_ports = QJsonArray2QListString(object["server_ports"].toArray());
        }
        if (object.contains("hop_interval")) hop_interval = object["hop_interval"].toString();
        if (object.contains("up_mbps")) up_mbps = object["up_mbps"].toInt();
        if (object.contains("down_mbps")) down_mbps = object["down_mbps"].toInt();
        if (protocol_version == "1") {
            if (object.contains("obfs")) obfs = object["obfs"].toString();
            if (object.contains("auth")) {
                auth = object["auth"].toString();
                auth_type = "BASE64";
            }
            if (object.contains("auth_str")) {
                auth = object["auth_str"].toString();
                auth_type = "STRING";
            }
            if (object.contains("recv_window_conn")) recv_window_conn = object["recv_window_conn"].toInt();
            if (object.contains("recv_window")) recv_window = object["recv_window"].toInt();
            if (object.contains("disable_mtu_discovery")) disable_mtu_discovery = object["disable_mtu_discovery"].toBool();
        } else {
            if (object.contains("obfs")) {
                auto obfsObj = object["obfs"].toObject();
                if (obfsObj.contains("password")) obfs = obfsObj["password"].toString();
            }
            if (object.contains("obfsPassword")) obfs = object["obfsPassword"].toString();
            if (object.contains("password")) password = object["password"].toString();
        }
        if (object.contains("tls")) tls->ParseFromJson(object["tls"].toObject());
        return true;
    }

    QString hysteria::ExportToLink()
    {
        QUrl url;
        QUrlQuery query;
        url.setScheme(protocol_version == "1" ? "hysteria" : "hysteria2");
        url.setHost(server);
        
        if (!name.isEmpty()) url.setFragment(name);

        if (protocol_version == "1") {
            if (!obfs.isEmpty()) {
                query.addQueryItem("obfsParam", QUrl::toPercentEncoding(obfs));
            }
            if (auth_type == "STRING" && !auth.isEmpty()) query.addQueryItem("auth", auth);
            if (recv_window_conn > 0) query.addQueryItem("recv_window_conn", QString::number(recv_window_conn));
            if (recv_window > 0) query.addQueryItem("recv_window", QString::number(recv_window));
            if (disable_mtu_discovery) query.addQueryItem("disable_mtu_discovery", "true");
        } else {
            if (password.contains(":")) {
                url.setUserName(SubStrBefore(password, ":"));
                url.setPassword(SubStrAfter(password, ":"));
            } else {
                url.setUserName(password);
            }
            if (!obfs.isEmpty()) {
                query.addQueryItem("obfs-password", QUrl::toPercentEncoding(obfs));
            }
        }
        
        if (up_mbps > 0) query.addQueryItem("upmbps", QString::number(up_mbps));
        if (down_mbps > 0) query.addQueryItem("downmbps", QString::number(down_mbps));
        
        if (!hop_interval.isEmpty()) query.addQueryItem("hop_interval", hop_interval);
        
        mergeUrlQuery(query, tls->ExportToLink());
        mergeUrlQuery(query, outbound::ExportToLink());
        
        if (!query.isEmpty()) url.setQuery(query);
        
        QString result;
        if (!server_ports.isEmpty()) {
            QStringList portList;
            for (const auto& port : server_ports) {
                QString modified = port;
                modified.replace(":", "-");
                portList.append(modified);
            }
            result = url.toString(QUrl::FullyEncoded);
            QString authority = url.authority(QUrl::FullyEncoded);
            int portStartIndex = result.indexOf(authority) + authority.length();
            result.insert(portStartIndex, ":" + portList.join(","));
        } else {
            url.setPort(server_port);
            result = url.toString(QUrl::FullyEncoded);
        }
        
        return result;
    }

    QJsonObject hysteria::ExportToJson()
    {
        QJsonObject object;
        object["type"] = protocol_version == "1" ? "hysteria" : "hysteria2";
        mergeJsonObjects(object, outbound::ExportToJson());
        if (!server_ports.isEmpty()) object["server_ports"] = QListStr2QJsonArray(server_ports);
        if (!hop_interval.isEmpty()) object["hop_interval"] = hop_interval;
        if (up_mbps > 0) object["up_mbps"] = up_mbps;
        if (down_mbps > 0) object["down_mbps"] = down_mbps;
        if (protocol_version == "1") {
            if (!obfs.isEmpty()) object["obfs"] = obfs;
            if (!auth.isEmpty()) {
                if (auth_type == "BASE64") object["auth"] = auth;
                if (auth_type == "STRING") object["auth_str"] = auth;
            }
            if (recv_window_conn > 0) object["recv_window_conn"] = recv_window_conn;
            if (recv_window > 0) object["recv_window"] = recv_window;
            if (disable_mtu_discovery) object["disable_mtu_discovery"] = disable_mtu_discovery;
        } else {
            if (!obfs.isEmpty()) {
                object["obfs"] = QJsonObject{
                    {"type", "salamander"},
                    {"password", obfs},
                };
            }
            if (!password.isEmpty()) object["password"] = password;
        }
        if (tls->enabled) object["tls"] = tls->ExportToJson();
        return object;
    }

    BuildResult hysteria::Build()
    {
        QJsonObject object;
        object["type"] = protocol_version == "1" ? "hysteria" : "hysteria2";
        mergeJsonObjects(object, outbound::Build().object);
        if (!server_ports.isEmpty()) object["server_ports"] = QListStr2QJsonArray(server_ports);
        if (!hop_interval.isEmpty()) object["hop_interval"] = hop_interval;
        if (up_mbps > 0) object["up_mbps"] = up_mbps;
        if (down_mbps > 0) object["down_mbps"] = down_mbps;
        if (protocol_version == "1") {
            if (!obfs.isEmpty()) object["obfs"] = obfs;
            if (!auth.isEmpty()) {
                if (auth_type == "BASE64") object["auth"] = auth;
                if (auth_type == "STRING") object["auth_str"] = auth;
            }
            if (recv_window_conn > 0) object["recv_window_conn"] = recv_window_conn;
            if (recv_window > 0) object["recv_window"] = recv_window;
            if (disable_mtu_discovery) object["disable_mtu_discovery"] = disable_mtu_discovery;
        } else {
            if (!obfs.isEmpty()) {
                object["obfs"] = QJsonObject{
                    {"type", "salamander"},
                    {"password", obfs},
                };
            }
            if (!password.isEmpty()) object["password"] = password;
        }
        if (tls->enabled) object["tls"] = tls->Build().object;
        return {object, ""};
    }

    QString hysteria::DisplayType()
    {
        return "Hysteria";
    }
}
