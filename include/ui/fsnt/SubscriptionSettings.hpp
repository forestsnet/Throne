#pragma once

class QWidget;

namespace Fsnt {
    // Настройки одной подписки: ссылка, имя, автообновление.
    //
    // Раньше всё это лежало только в расширенном режиме, в окне управления
    // группами, куда человек из простого интерфейса не заходит вовсе.
    void EditSubscription(QWidget *parent, int gid);
} // namespace Fsnt
