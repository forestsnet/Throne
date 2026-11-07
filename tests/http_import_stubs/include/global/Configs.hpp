#pragma once
#include <QString>
#include <functional>
#include <memory>
namespace Configs {
struct GroupsRepo;
struct RequestTestSettings {
    bool net_use_proxy = false;
    bool spmode_system_proxy = false;
    bool spmode_vpn = false;
    bool kill_switch = false;
    bool disable_mixed_inbound = false;
    bool inbound_auth = false;
    bool net_insecure = false;
    int started_id = -1919;
    int inbound_socks_port = 0;
    QString inbound_user, inbound_pass;
    QString GetUserAgent() const { return QStringLiteral("request-test"); }
};
struct RequestTestDataManager {
    std::shared_ptr<RequestTestSettings> settingsRepo = std::make_shared<RequestTestSettings>();
    std::shared_ptr<GroupsRepo> groupsRepo;
};
inline RequestTestDataManager testDataManager;
inline RequestTestDataManager *dataManager = &testDataManager;
inline QString GetBasePath() { return {}; }
}
inline void MW_show_log(const QString &) {}
inline void runOnUiThread(const std::function<void()> &callback, bool = false) { callback(); }
