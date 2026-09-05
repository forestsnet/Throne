#include "include/configs/RoutePresets.hpp"

#include <QCoreApplication>

#include "include/database/RoutesRepo.h"
#include "include/database/entities/RouteProfile.h"
#include "include/database/entities/RouteRule.h"
#include "include/global/Configs.hpp"

namespace {
    constexpr auto kPresetAll = "all";
    constexpr auto kPresetBypassLocal = "bypass-local";
    constexpr auto kPresetBlockedOnly = "blocked-only";
    constexpr auto kPresetGaming = "gaming";
    constexpr auto kPresetServices = "services";

    // Наборы правил тянутся сами по .srs: локальные geoip.dat/geosite.dat для
    // пресетов не нужны, и первый запуск не упирается в их загрузку.
    const QStringList kDomesticSets = {QStringLiteral("geosite-ru"), QStringLiteral("geoip-ru")};
    const QStringList kBlockedSets = {QStringLiteral("geosite-ru-blocked"),
                                      QStringLiteral("geoip-ru-blocked")};

    // Игровые площадки и Discord: голосовой чат для игры такая же обязательная
    // часть, как сам лаунчер, и разносить их по разным схемам бессмысленно.
    const QStringList kGamingSets = {
        QStringLiteral("geosite-category-games"), QStringLiteral("geosite-steam"),
        QStringLiteral("geosite-epicgames"),      QStringLiteral("geosite-riot"),
        QStringLiteral("geosite-blizzard"),       QStringLiteral("geosite-playstation"),
        QStringLiteral("geosite-xbox"),           QStringLiteral("geosite-nintendo"),
        QStringLiteral("geosite-roblox"),         QStringLiteral("geosite-ea"),
        QStringLiteral("geosite-ubisoft"),        QStringLiteral("geosite-discord"),
    };

    // Видео, соцсети, мессенджеры и ИИ. geosite-meta намеренно нет: это домены
    // проекта Clash.Meta, а не Facebook, и по имени их легко перепутать.
    const QStringList kMediaSets = {
        QStringLiteral("geosite-youtube"),   QStringLiteral("geosite-twitch"),
        QStringLiteral("geosite-netflix"),   QStringLiteral("geosite-spotify"),
        QStringLiteral("geosite-instagram"), QStringLiteral("geosite-facebook"),
        QStringLiteral("geosite-whatsapp"),  QStringLiteral("geosite-x"),
        QStringLiteral("geosite-tiktok"),    QStringLiteral("geosite-openai"),
        QStringLiteral("geosite-telegram"),  QStringLiteral("geoip-telegram"),
    };

    std::shared_ptr<Configs::RouteRule> dnsRule() {
        auto rule = std::make_shared<Configs::RouteRule>();
        rule->name = QStringLiteral("Route DNS");
        rule->action = QStringLiteral("hijack-dns");
        rule->protocol = QStringLiteral("dns");
        return rule;
    }

    // Локальная сеть мимо туннеля. Без этого правила в режиме TUN перестают
    // отвечать принтер, роутер и всё остальное в домашней сети.
    std::shared_ptr<Configs::RouteRule> privateDirectRule() {
        auto rule = std::make_shared<Configs::RouteRule>();
        rule->name = QStringLiteral("Local network");
        rule->ip_is_private = true;
        rule->outboundID = Configs::directID;
        return rule;
    }

    std::shared_ptr<Configs::RouteRule> ruleSetRule(const QString &name, const QStringList &sets,
                                                   const int outbound) {
        auto rule = std::make_shared<Configs::RouteRule>();
        rule->name = name;
        rule->rule_set = sets;
        rule->outboundID = outbound;
        return rule;
    }

    void fillPreset(const QString &key, Configs::RouteProfile &profile) {
        profile.Rules.clear();
        profile.isRaw = false;
        profile.isRemote = false;
        profile.Rules << dnsRule() << privateDirectRule();

        if (key == QLatin1String(kPresetBypassLocal)) {
            profile.defaultOutboundID = Configs::proxyID;
            profile.Rules << ruleSetRule(QStringLiteral("Domestic sites direct"), kDomesticSets,
                                         Configs::directID);
            return;
        }
        if (key == QLatin1String(kPresetGaming)) {
            profile.defaultOutboundID = Configs::directID;
            profile.Rules << ruleSetRule(QStringLiteral("Games via VPN"), kGamingSets,
                                         Configs::proxyID);
            return;
        }
        if (key == QLatin1String(kPresetServices)) {
            profile.defaultOutboundID = Configs::directID;
            profile.Rules << ruleSetRule(QStringLiteral("Games via VPN"), kGamingSets,
                                         Configs::proxyID);
            profile.Rules << ruleSetRule(QStringLiteral("Media and social via VPN"), kMediaSets,
                                         Configs::proxyID);
            return;
        }
        if (key == QLatin1String(kPresetBlockedOnly)) {
            // Наоборот: по умолчанию напрямую, в туннель уходит только то, что
            // без него не открывается.
            profile.defaultOutboundID = Configs::directID;
            profile.Rules << ruleSetRule(QStringLiteral("Blocked sites via VPN"), kBlockedSets,
                                         Configs::proxyID);
            return;
        }
        profile.defaultOutboundID = Configs::proxyID;
    }
}

namespace Configs {
    QList<RoutePreset> RoutePresets() {
        return {
            {QString::fromLatin1(kPresetAll),
             QCoreApplication::translate("RoutePresets", "Everything through the VPN"),
             QCoreApplication::translate(
                 "RoutePresets",
                 "Every app and every site goes through the tunnel. The safest choice and the "
                 "one to pick if you are not sure; the local network still works.")},
            {QString::fromLatin1(kPresetBypassLocal),
             QCoreApplication::translate("RoutePresets", "Except domestic sites"),
             QCoreApplication::translate(
                 "RoutePresets",
                 "Russian sites and addresses go directly, everything else through the tunnel. "
                 "Banking and government sites keep working and stay fast.")},
            {QString::fromLatin1(kPresetBlockedOnly),
             QCoreApplication::translate("RoutePresets", "Blocked sites only"),
             QCoreApplication::translate(
                 "RoutePresets",
                 "Only what is blocked goes through the tunnel, the rest directly. The fastest "
                 "option, but a site missing from the list will not be unblocked.")},
            {QString::fromLatin1(kPresetGaming),
             QCoreApplication::translate("RoutePresets", "Games and Discord"),
             QCoreApplication::translate(
                 "RoutePresets",
                 "Steam, Epic, Riot, Battle.net, PlayStation, Xbox, Roblox and Discord go through "
                 "the tunnel, everything else directly. Pick a nearby server: game traffic now "
                 "goes through it, and distance turns into ping.")},
            {QString::fromLatin1(kPresetServices),
             QCoreApplication::translate("RoutePresets", "Games, video and social"),
             QCoreApplication::translate(
                 "RoutePresets",
                 "The same plus YouTube, Twitch, Instagram, WhatsApp, Telegram, TikTok and "
                 "ChatGPT. Banking and government sites stay direct, so they keep working.")},
        };
    }

    QString RoutePresetProfileName(const QString &key) {
        if (key == QLatin1String(kPresetAll)) return QStringLiteral("VPN: all traffic");
        if (key == QLatin1String(kPresetBypassLocal)) return QStringLiteral("VPN: except domestic");
        if (key == QLatin1String(kPresetBlockedOnly)) return QStringLiteral("VPN: blocked only");
        if (key == QLatin1String(kPresetGaming)) return QStringLiteral("VPN: games and Discord");
        if (key == QLatin1String(kPresetServices)) return QStringLiteral("VPN: games, video and social");
        return {};
    }

    QString RoutePresetKeyOf(const QString &profileName) {
        for (const auto &preset : RoutePresets()) {
            if (RoutePresetProfileName(preset.key) == profileName) return preset.key;
        }
        return {};
    }

    bool IsUntouchedDefaultProfile(const int profileId) {
        const auto profile = dataManager->routesRepo->GetRouteProfile(profileId);
        if (profile == nullptr || profile->name != QStringLiteral("Default")) return false;
        // У стокового ровно одно правило — перехват DNS. Появились другие —
        // значит его правили, и подменять выбор молча уже нельзя.
        return profile->Rules.size() <= 1 && !profile->isRaw && !profile->isRemote;
    }

    int EnsureRoutePreset(const QString &key) {
        const QString name = RoutePresetProfileName(key);
        if (name.isEmpty()) return INVALID_ID;

        for (const auto &existing : dataManager->routesRepo->GetAllRouteProfiles()) {
            if (existing == nullptr || existing->name != name) continue;
            fillPreset(key, *existing);
            dataManager->routesRepo->Save(existing);
            return existing->id;
        }

        auto profile = RoutesRepo::NewRouteProfile();
        profile->name = name;
        fillPreset(key, *profile);
        if (!dataManager->routesRepo->AddRouteProfile(profile)) return INVALID_ID;
        return profile->id;
    }
} // namespace Configs
