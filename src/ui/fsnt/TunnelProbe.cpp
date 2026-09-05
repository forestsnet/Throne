#include "include/ui/fsnt/TunnelProbe.hpp"

#include <QTimer>

#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/Utils.hpp"

namespace {
    // Сколько ждать после подключения. Маршруты встают не мгновенно, и проба
    // сразу после старта ловила бы ложные отказы.
    constexpr int kProbeDelayMs = 6000;

    // Мелкая страница без содержимого: нам нужен сам факт ответа. Тот же адрес
    // Windows использует для проверки связи, так что он редко бывает закрыт.
    const auto kProbeUrl = QStringLiteral("http://www.msftconnecttest.com/connecttest.txt");
}

namespace Fsnt {
    TunnelProbe::TunnelProbe(QObject *parent) : QObject(parent) {
        m_delay = new QTimer(this);
        m_delay->setSingleShot(true);
        m_delay->setInterval(kProbeDelayMs);
        connect(m_delay, &QTimer::timeout, this, &TunnelProbe::run);
    }

    void TunnelProbe::start() {
        ++m_generation;
        m_delay->start();
    }

    void TunnelProbe::cancel() {
        ++m_generation;
        m_delay->stop();
    }

    void TunnelProbe::run() {
        const auto &settings = Configs::dataManager->settingsRepo;
        // Проверять есть смысл только на живом туннеле в режиме TUN: в режиме
        // системного прокси этот путь не используется вовсе.
        if (!settings->core_running || settings->started_id < 0 || !settings->spmode_vpn) return;

        const int generation = m_generation;
        QPointer<TunnelProbe> self = this;
        runOnNewThread([self, generation] {
            // useProxy = false: идём через систему и TUN, как обычная программа,
            // а не через локальный прокси клиента.
            const auto response = Configs_network::NetworkRequestHelper::HttpGet(kProbeUrl);
            if (response.error.isEmpty()) return;

            runOnUiThread([self, generation] {
                if (self.isNull() || self->m_generation != generation) return;
                const auto &settings = Configs::dataManager->settingsRepo;
                // Пока проба шла, пользователь мог отключиться — тогда молчим.
                if (!settings->core_running || settings->started_id < 0) return;
                emit self->tunnelDead();
            });
        });
    }
} // namespace Fsnt
