#pragma once
#include "include/configs/common/Outbound.h"
#include "include/configs/common/xrayMultiplex.h"
#include "include/configs/common/xrayStreamSetting.h"

namespace Configs {
    inline QStringList xrayFlows = {"xtls-rprx-vision", "xtls-rprx-vision-udp443"};

    class xrayVless : public outbound {
        public:
        QString uuid;
        QString encryption = "none";
        QString flow;
        std::shared_ptr<xrayStreamSetting> streamSetting = std::make_shared<xrayStreamSetting>();
        std::shared_ptr<xrayMultiplex> multiplex = std::make_shared<xrayMultiplex>();

        xrayVless() : outbound() {
            _add(new configItem("uuid", &uuid, string));
            _add(new configItem("encryption", &encryption, string));
            _add(new configItem("flow", &flow, string));
            _add(new configItem("streamSetting", dynamic_cast<JsonStore *>(streamSetting.get()), jsonStore));
            _add(new configItem("multiplex", dynamic_cast<JsonStore *>(multiplex.get()), jsonStore));
        }

        bool ParseFromLink(const QString& link) override;
        bool ParseFromJson(const QJsonObject& object) override;
        QString ExportToLink() override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
        BuildResult BuildXray() override;

        std::shared_ptr<xrayStreamSetting> GetXrayStream() override { return streamSetting; }

        std::shared_ptr<xrayMultiplex> GetXrayMultiplex() override { return multiplex; }

        QString DisplayType() override {
            return "VLESS (Xray)";
        }
        bool IsXray() override {
           return true;
        }
    };
}
