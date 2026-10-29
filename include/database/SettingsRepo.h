#pragma once

#include "Database.h"
#include "include/global/Const.hpp"
#include "include/sys/UrlScheme.hpp"
#include <QMutexLocker>
#include <QJsonObject>
#include <QMap>
#include <QKeySequence>
#include <atomic>

#ifdef Q_OS_WIN
#include "include/sys/windows/WinVersion.h"
#endif

namespace Configs {
    // Loopback/broadcast are deliberately absent: routing them into the tun breaks the sing-box <-> Xray bridges and local DNS.
    inline QStringList defaultTunPrivateRanges() {
        return {"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16", "224.0.0.0/4",
                "fc00::/7", "fe80::/10", "ff00::/8"};
    }

    class SettingsRepo {
    private:
        Database& db;

        QMap<QString, bool*>        boolMap;
        QMap<QString, int*>         intMap;
        QMap<QString, QString*>     stringMap;
        QMap<QString, QStringList*> stringListMap;

        void initMaps();
        void createTables() const;
        void loadAllSettings();
        void saveAllSettings() const;

    public:
        bool noSave = false;

        explicit SettingsRepo(Database& database);
        
        bool Save();
        
        // Runtime state and flags below are never persisted.
        QString core_socket_name = "";
        int started_id = NoProfileId;
        bool core_running = false;
        bool prepare_exit = false;
        bool spmode_vpn = false;
        bool spmode_system_proxy = false;
        QString appdataDir = "";
        QStringList ignoreConnTag = {};
        int imported_count = 0;
        bool refreshing_group_list = false;
        bool refreshing_group = false;
        std::atomic<int> resolve_count = 0;

        QStringList argv = {};
        bool flag_use_appdata = false;
        bool flag_many = false;
        bool flag_tray = false;
        bool flag_debug = false;
        bool flag_restart_tun_on = false;

        // Persisted settings.
        QString mainWindowGeometry;
        QString log_level = "info";
        // generate_204 отвечает пустым 204 и живёт ровно для таких проверок.
        QString test_latency_url = "https://www.gstatic.com/generate_204";
        // Fetched WITHOUT any proxy, so it must be reachable directly; empty falls back to the OS.
        QString direct_test_url = "";
        // Both keys are shared with the Android app's settings table; the provider is a Configs_network::ExitLookupProviders() id.
        bool exit_country_lookup = true;
        QString exit_lookup_provider = "auto";
        int url_test_timeout_ms = 3000;
        // Чем меряем задержку: 0 ICMP, 1 TCP до порта из конфига, 2 рукопожатие
        // TLS, 3 запрос GET через туннель, 4 запрос HEAD. По умолчанию запрос:
        // он один меряет весь путь целиком, а не дорогу до сервера.
        int ping_kind = 3;
        // Подсказку про переключение подписок показываем один раз — в тот
        // момент, когда их стало больше одной.
        bool hint_switch_subs = false;
        // Системное уведомление о новой версии. Колокольчик в окне остаётся
        // всегда — выключается именно всплывающее окно системы.
        bool notify_update_system = true;
        // Туннель был поднят, когда клиент ушёл обновляться: после перезапуска
        // возвращаем и сервер, и режим, даже если «подключаться при запуске»
        // выключено. Человек не просил его отключать — он просил обновиться.
        bool resume_after_update = false;
        // Ставить новые версии самому, без вопросов. Спрашиваем об этом один
        // раз — при первом запуске новой версии.
        bool auto_update = false;
        // Версия, с которой клиент запускался в прошлый раз: по ней и видно,
        // что версия сменилась.
        QString last_run_version = "";
        bool notify_subscription = true;
        bool notify_connection = true;
        // Версия, о которой уже сказали: второй раз не всплываем.
        QString update_seen_version = "";
        bool disable_tray = false;
        int test_concurrent = 10;
        bool disable_traffic_stats = false;
        int current_group = 0;
        QString mux_protocol = "smux";
        bool mux_padding = false;
        int mux_concurrency = 8;
        bool mux_default_on = false;
        // "built-in" = sing-box tls.fragment, "custom" = hiddify dialer-level tls_fragment.
        QString fragment_implementation = "built-in";
        bool fragment_default_on = false;
        // "min-max" ranges, custom implementation only: bytes per ClientHello fragment, and ms to sleep between bursts.
        QString fragment_size = "10-100";
        QString fragment_sleep = "2-5";
        // TLS tricks = mixed-case SNI.
        bool tls_tricks_default_on = false;
        // SNI to forge and how the real server rejects the forged segment; profiles with an empty field of their own inherit these.
        QString tls_spoof = "";
        QString tls_spoof_method = "";
        bool tls_spoof_default_on = false;

        // Shared by HTTP/2 and the QUIC outbounds; empty / 0 means "leave the core's default" and is omitted from the config.
        QString h2_idle_timeout = "";
        QString h2_keep_alive_period = "";
        QString h2_stream_receive_window = "";
        QString h2_connection_receive_window = "";
        int h2_max_concurrent_streams = 0;
        int quic_initial_packet_size = 0;
        bool quic_disable_path_mtu_discovery = false;
        QString theme = "qdarkstyle";
        int ui_mode = -1;   // -1 = не задан; см. Fsnt::ResolveInitialUiMode
        // JSON-массив имён избранных серверов. Именно имён, а не id:
        // при sub_clear (умолчание upstream) обновление подписки удаляет все
        // профили и создаёт заново, поэтому id живут только до обновления.
        QString favorite_profiles = "[]";
        int simple_transport = 0;   // 0 = TUN, 1 = системный прокси; только для простого режима
        bool onboarding_done = false;   // мастер первого запуска простого режима
        bool conflict_warning_disabled = false;   // предупреждение о конфликтующих программах
        // Сервер, выбранный пользователем в простом режиме, — по имени.
        // Отдельно от remember_id: тот означает «последний запущенный» и
        // управляет автозапуском, а выбор строки в списке запускать ничего не
        // должен. По имени, а не по id, по той же причине, что и избранное.
        QString simple_selected_server = "";
        int language = 4;
        QString font = "";
        int font_size = 0;
        QString mw_size = "";
        bool log_enable_include = false;
        bool log_enable_exclude = false;
        QStringList log_include_keyword = {};
        QStringList log_include_regex = {};
        QStringList log_exclude_keyword = {};
        QStringList log_exclude_regex = {};
        bool start_minimal = false;
        int max_log_line = 500;
        // Log view font; empty family / 0 size fall back to the built-in monospace list / the app font size.
        QString log_font_family = "";
        int log_font_size = 0;
        // On-disk diagnostic log only; log_level is the core's browser verbosity.
        QString log_file_level = "debug";
        QString splitter_state = "";
        bool enable_stats = true;
        int stats_tab = 0; // either connection or log
        // LiveChartState of the traffic graph; keys are Stats::TrafficLine keys.
        QStringList traffic_graph_hidden = {};
        // Bit 0 hides download, bit 1 upload.
        int traffic_graph_hidden_dirs = 0;
        int traffic_graph_range = 1;
        bool traffic_graph_smooth = true;
        bool traffic_graph_markers = true;
        // Stats::ConnectionSort; 0 == Stats::Default, the core's own ordering.
        int connection_sort = 0;
        bool connection_sort_asc = false;
        // Days of hour-resolution history to retain (the 48h minute-resolution window is fixed); clamped to >= 1 in use.
        int traffic_stats_retention_days = 90;
        bool disable_traffic_aggregation = false;
        int speed_test_mode = TestConfig::FULL;
        int speed_test_timeout_ms = 5000;
        QString simple_dl_url = "http://cachefly.cachefly.net/1mb.test";
        bool allow_beta_update = false;
        bool use_custom_icons = false;
        bool follow_status_in_taskbar = true;
        bool skip_delete_confirmation = false;
        bool show_config_security = false;
        // -1 until a filter column has been used.
        int last_filter_column = -1;

        // Mirrors of the registrations we last wrote to the OS; startup re-registers only when they differ.
        QString url_scheme_mirror = "";
        bool url_scheme_auto_register = UrlScheme_AutoRegisterByDefault();
        QString file_assoc_mirror = "";
        bool file_assoc_auto_register = false;

        // Network
        bool net_use_proxy = false;
        bool net_insecure = false;
        bool reset_proxy_on_disable_sp = false;

        // Subscription
        QString user_agent = ""; // set at main.cpp
        // Configs::subTlsVersion / Configs::subHttpVersion values.
        int sub_tls_version = 0;
        int sub_http_version = 0;
        // Sign encodes enabled (negative = off), magnitude = interval minutes (ignored if < 30).
        // Включено по умолчанию: в простом режиме обновлять подписку вручную
        // человеку негде, а устаревший список серверов выглядит как поломка.
        int sub_auto_update = 120;
        // Follow a server's profile-update-interval instead of sub_auto_update's minutes.
        bool sub_respect_server_interval = false;
        // Когда в последний раз сказали про новую версию: напоминаем не чаще
        // раза в три дня, иначе уведомление превращается в фон.
        qint64 update_notified_at = 0;
        bool sub_clear = true;
        bool sub_show_change_popup = true;
        bool sub_send_hwid = true;
        QString sub_custom_hwid_params = "";
        bool allow_stopping_active_profile = false;

        // Security
        bool skip_cert = false;
        QString utlsFingerprint = "firefox";
        bool disable_run_admin = false; // windows only
        bool use_mozilla_certs = false;
        bool kill_switch = false;

        // Remote API
        bool remote_api_enable = false;
        bool remote_api_lan = false;
        bool remote_api_admin_lan = false;
        int remote_api_port = 9095;
        QString remote_api_key = "";
        // CIDRs separated by commas or whitespace; empty allows every LAN address.
        QString remote_api_allow = "";

        // WebDAV backup and this device's identity: device-local, see BackupArchive::DeviceLocalSettingKeys().
        QString webdav_server = "";
        QString webdav_user = "";
        QString webdav_pass = "";
        QString webdav_path = "Throne";
        bool webdav_use_proxy = false;
        // Generated on first use; an empty name means the host name.
        QString device_id = "";
        QString device_name = "";

        // Remember
        bool remember_system_proxy = false;
        bool remember_tun = false;
        int remember_id = NoProfileId;
        bool remember_enable = true;
        bool windows_set_admin = true;
        QMap<QString, QKeySequence> shortcuts;

        // Routing
        int current_route_id = 1;
        // Same sign-encoded interval scheme as sub_auto_update.
        int route_auto_update = -1440;
        qint64 route_auto_update_last = 0;
        // DNS для трафика через VPN и для прямого — по умолчанию Quad9 на обоих.
        // Только DoH: обычный запрос по 53 порту виден провайдеру целиком, и
        // прятать трафик в туннель, оставляя запросы открытыми, бессмысленно.
        QString remote_dns = "https://9.9.9.9/dns-query";
        bool remote_dns_disable_ipv6 = false;
        // Прямой DNS разрешает имена наших же серверов, причём не только до
        // туннеля, но и под ним: имя сервера нужно каждому новому соединению.
        // Поэтому 53-й порт сюда не годится — strict_route на Windows ставит
        // фильтр, который рубит весь 53-й мимо туннеля, включая запросы самого
        // ядра, и имя сервера перестаёт разрешаться вообще. По той же причине
        // здесь адрес, а не имя: имя пришлось бы разрешать системным
        // резолвером, а он под туннелем приводит обратно к нам же. DoT к
        // Яндексу по адресу: 853-й порт под фильтр не попадает, сертификат
        // 77.88.8.8 в себе содержит, и у нашей аудитории он доступен.
        QString direct_dns = "tls://77.88.8.8";
        // Подбор рабочего прямого DNS. Он разрешает имя нашего же сервера, и
        // когда провайдер режет выбранный транспорт, человек остаётся без
        // интернета вообще: туннель уже забрал маршруты, а подключаться некуда.
        bool direct_dns_auto = true;
        // То, что человек вписал сам. Пробуется раньше каталога.
        QStringList dns_custom_servers = {};
        // Чем подбор закончился в прошлый раз: подсказка в настройках и в
        // диагностике, сама настройка от этого не меняется.
        QString direct_dns_effective = "";
        bool direct_dns_disable_ipv6 = false;
        // Servers typed into the remote / direct DNS fields, newest first.
        QStringList remote_dns_history = {};
        QStringList direct_dns_history = {};
        int dns_cache_capacity = 65536;
        bool dns_disable_cache = false;
        bool dns_disable_expire = false;
        bool dns_persist_cache = false;
        bool dns_reverse_mapping = false;
        bool enable_dns_routing = true;
        bool use_dns_object = false;
        QString dns_object = "";
        QString dns_final_out = "remote";
        bool dns_optimistic = false;
        QString dns_optimistic_timeout = "";
        QString dns_query_timeout = "";
        bool dns_use_hosts = false;
        bool dns_predefined_enable = true;
        QStringList dns_predefined_rules = {"127.0.0.1 localhost"};
        QString resolve_domain_strategy = "prefer_ipv4";
        QString default_domain_strategy = "prefer_ipv4";
        int ruleset_mirror = Mirrors::GITHUB;

        // Socks & HTTP Inbound
        bool disable_mixed_inbound = false;
        QString inbound_address = "127.0.0.1";
        int inbound_socks_port = 2080; // Mixed, actually
        bool random_inbound_port = true;
        QString custom_inbound = "{\"inbounds\": []}";
        QString proxy_scheme = "{ip}:{port}";
        bool inbound_auth = false;
        QString inbound_user = "";
        QString inbound_pass = "";

        // Routing
        QString custom_route_global = "{\"rules\": []}";
        QString active_routing = "Default";
        bool adblock_enable = false;

        // VPN
        // iCloud Private Relay выключает себя, только если система видит VPN, а
        // видит она его лишь у туннелей через NetworkExtension. Ядро открывает
        // utun напрямую, поэтому реле остаётся включённым, трафик Safari идёт в
        // него, и наружу светится выход Apple вместо выбранной страны. Apple
        // просит сети, которым реле не подходит, отвечать NXDOMAIN на его имена
        // — тогда устройство честно говорит человеку, что реле тут недоступно.
        bool block_private_relay = true;
        bool fake_dns = false;
        bool fakeip_disable_ipv6 = false;
        bool enable_tun_routing = true;
#ifdef Q_OS_MACOS
        bool vpn_strict_route = false;
#elif defined(Q_OS_WIN)
        bool vpn_strict_route = WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1507);
#else
        bool vpn_strict_route = false;
#endif
        // Linux only; while on, the host cannot act as a network gateway.
        bool vpn_auto_redirect = true;
        // Only UDP and ICMP reach the bridge: pre-match aborts at the sniff rule for TCP.
        bool vpn_l3_bridge = false;
        int vpn_mtu = 1420;
        bool disable_private_range_bypass = false;
        QStringList vpn_private_ranges = defaultTunPrivateRanges();
        bool vpn_ipv6 = false;
        QString vpn_tun_ipv4_cidr = "172.19.0.1/24";
        QString vpn_tun_ipv6_cidr = "fdfe:dcba:9876::1/96";
        bool disable_privilege_req = false;

        // NTP
        bool enable_ntp = false;
        QString ntp_server_address = "";
        int ntp_server_port = 0;
        QString ntp_interval = "";
        QString ntp_outbound = "direct"; // "direct" or "proxy"

        // Warp
        bool enable_warp = false;
        QString warp_private_key = "";
        QString warp_public_key = "";
        QStringList warp_ifc_addrs = {};
        QString warp_ep = "";
        QStringList warp_reserved = {};
        bool warp_tos_accepted = false;
        QString warp_mode = "wireguard"; // "wireguard" or "masque"
        QString warp_masque_private_key = "";
        QString warp_masque_peer_public_key = "";
        QString warp_masque_ep = "";
        QStringList warp_masque_ifc_addrs = {};
        QString warp_masque_sni = "consumer-masque.cloudflareclient.com";
        int warp_masque_http_mode = 0; // 0 = HTTP/3 with fallback, 1 = HTTP/3 only, 2 = HTTP/2
        QStringList warp_api_hosts = {}; // registration API domains, tried in order; empty = api.cloudflareclient.com

        // Hotkey
        QString hotkey_mainwindow = "";
        QString hotkey_group = "";
        QString hotkey_route = "";
        QString hotkey_system_proxy_menu = "";
        QString hotkey_toggle_system_proxy = "";
        QString hotkey_toggle_connection = "";
        QString hotkey_toggle_tun = "";

        // Core
        int core_box_clash_api = -9090;
        QString core_box_clash_listen_addr = "127.0.0.1";
        QString core_box_clash_api_secret = "";
        // Port only publishes the dashboard; the service itself also carries the stats tracker.
        int core_box_api_port = -9091;
        QString core_box_api_secret = "";
        QString core_box_underlying_dns = "";
        int core_dns_in_port = 5533;

        // Xray
        QString xray_log_level = "info";
        int xray_mux_concurrency = 8;
        bool xray_mux_default_on = false;
        Xray::XrayVlessPreference xray_vless_preference = Xray::XhttpAndReality;
        // Fetched on demand into GetBasePath(), which the core exposes to Xray via XRAY_LOCATION_ASSET.
        QString xray_geoip_url = "https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geoip.dat";
        QString xray_geosite_url = "https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geosite.dat";
        // Last 5 hand-typed URLs per field, offered alongside the built-in providers.
        QStringList xray_geoip_url_history = {};
        QStringList xray_geosite_url_history = {};

        // Extra Core Paths
        QStringList extraCorePaths = {};

        // Last 5 custom entries per field.
        QStringList dial_bind_interface_history = {};
        QStringList dial_inet4_bind_address_history = {};
        QStringList dial_inet6_bind_address_history = {};

        void UpdateStartedId(int id);

        [[nodiscard]] QString GetUserAgent(bool isDefault = false) const;
        
        [[nodiscard]] QStringList GetExtraCorePaths() const;
        bool AddExtraCorePath(const QString &path);
    };
}
