#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <include/global/Utils.hpp>

int Mac_Run_Command(QString command, const QString &prompt) {
    // Через системное окно авторизации, а не через Терминал.
    //
    // Раньше клиент открывал Терминал и писал туда команду с sudo. Строка при
    // этом собиралась для system(): путь к ядру заключался в одинарные кавычки
    // внутри одинарных кавычек osascript -e '…'. Пока путь был без пробелов,
    // склейка случайно оставалась рабочей; стоило приложению лежать в папке
    // вроде «Throne — копия.app», как команда разваливалась, osascript падал, и
    // по кнопке «Открыть Терминал» не происходило ровно ничего.
    //
    // do shell script … with administrator privileges просит саму macOS
    // показать окно с полем пароля. Пароль вводится в системное окно, минуя
    // нас, Терминал не нужен, и разрешение на автоматизацию — тоже.
    const auto forAppleScript = [](QString text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    };

    // Заголовок окна macOS ставит свой — «osascript хочет внести изменения».
    // Человеку это слово не говорит ничего, поэтому объясняем в подписи, кто и
    // зачем просит пароль.
    QString script = QStringLiteral("do shell script \"%1\"").arg(forAppleScript(command));
    if (!prompt.isEmpty()) {
        script += QStringLiteral(" with prompt \"%1\"").arg(forAppleScript(QString(prompt)));
    }
    script += QStringLiteral(" with administrator privileges");

    // Аргументом, а не строкой для оболочки: кавычки и пробелы в пути больше
    // ничего не ломают.
    QProcess osascript;
    osascript.start(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
    if (!osascript.waitForStarted(5000)) return -1;
    // Ждём столько, сколько человек будет искать пароль.
    osascript.waitForFinished(-1);
    return osascript.exitCode();
}
