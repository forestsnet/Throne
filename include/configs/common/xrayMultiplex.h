#pragma once
#include "include/configs/baseConfig.h"

namespace Configs {
    class xrayMultiplex : public baseConfig {
        public:
        bool enabled = false;
        int concurrency = 0;
        int xudpConcurrency = 16;

        xrayMultiplex() {
            _add(new configItem("enabled", &enabled, boolean));
            _add(new configItem("concurrency", &concurrency, integer));
            _add(new configItem("xudpConcurrency", &xudpConcurrency, integer));
        }

        bool ParseFromLink(const QString& link) override;
        bool ParseFromJson(const QJsonObject& object) override;
        QString ExportToLink() override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };
}
