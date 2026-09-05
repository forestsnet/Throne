#pragma once

#include <QObject>

class QMenu;
class QSystemTrayIcon;

namespace Fsnt {
    // Меню значка приложения: в macOS это пункт в строке меню, в Windows и
    // Linux — значок в трее. Объект один, потому что виджет один и тот же.
    //
    // Своё меню нужно потому, что штатное собрано для инженерного режима:
    // «Select Routing», «OTP Codes», «Restart Proxy», подменю режимов прокси.
    // Человеку, который поставил клиент чтобы нажать одну кнопку, там нечего
    // выбрать, а половина пунктов вредна.
    //
    // Содержимое пересобирается перед каждым показом: состояние, сервер и
    // скорость живут своей жизнью, а держать их в актуальном виде по таймеру
    // значило бы дёргать базу впустую, пока меню закрыто.
    class TrayMenu : public QObject {
        Q_OBJECT

    public:
        // Забирает значок под своё меню. Родитель — окно простого режима.
        TrayMenu(QSystemTrayIcon *tray, QObject *parent);

    private:
        void rebuild();
        void addStatusSection();
        void addServerSection();
        // Переключиться на сервер: если туннель поднят, ядро перезапустится с
        // новым профилем, иначе выбор просто запомнится до подключения.
        void chooseServer(int profileId, const QString &name);

        QSystemTrayIcon *m_tray = nullptr;
        QMenu *m_menu = nullptr;
    };
} // namespace Fsnt
