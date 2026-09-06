#include "include/global/DeviceDetailsHelper.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QRegularExpression>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QSysInfo>
#include <QFile>

#include "include/global/Configs.hpp"
#include <vector>   
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include "include/sys/windows/WinVersion.h"
#include <include/sys/Process.hpp>
#include <Wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")
#endif

#ifdef Q_OS_WIN
static QString queryWmiProperty(const QString& wmiClass, const QString& property) {
    HRESULT hres;

    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) return QString();

    hres = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL
    );
    if (FAILED(hres) && hres != RPC_E_TOO_LATE) {
        CoUninitialize();
        return QString();
    }

    IWbemLocator* pLoc = NULL;
    hres = CoCreateInstance(
        CLSID_WbemLocator, 0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc
    );
    if (FAILED(hres)) {
        CoUninitialize();
        return QString();
    }

    IWbemServices* pSvc = NULL;
    BSTR bstrNamespace = SysAllocString(L"ROOT\\CIMV2");
    hres = pLoc->ConnectServer(
        bstrNamespace,
        NULL, NULL, NULL, 0, NULL, 0, &pSvc
    );
    SysFreeString(bstrNamespace);
    if (FAILED(hres)) {
        pLoc->Release();
        CoUninitialize();
        return QString();
    }

    hres = CoSetProxyBlanket(
        pSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE
    );
    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return QString();
    }

    IEnumWbemClassObject* pEnumerator = NULL;
    QString query = QString("SELECT %1 FROM %2").arg(property, wmiClass);
    BSTR bstrWQL = SysAllocString(L"WQL");
    BSTR bstrQuery = SysAllocString(query.toStdWString().c_str());
    hres = pSvc->ExecQuery(
        bstrWQL,
        bstrQuery,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &pEnumerator
    );
    SysFreeString(bstrWQL);
    SysFreeString(bstrQuery);
    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return QString();
    }

    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;
    QString result;

    if (pEnumerator) {
        HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (uReturn) {
            VARIANT vtProp;
            VariantInit(&vtProp);
            hr = pclsObj->Get(property.toStdWString().c_str(), 0, &vtProp, 0, 0);
            if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR) {
                result = QString::fromWCharArray(vtProp.bstrVal);
            }
            VariantClear(&vtProp);
            pclsObj->Release();
        }
        pEnumerator->Release();
    }

    pSvc->Release();
    pLoc->Release();
    CoUninitialize();
    return result;
}

static QString winBaseBoard() {
    return queryWmiProperty("Win32_BaseBoard", "Product");
}

static QString winModel() {
    return queryWmiProperty("Win32_ComputerSystem", "Model");
}
#endif


namespace {
    // Идентификатор устройства панели видят в заголовке x-hwid и по нему считают
    // устройства подписки. Панель ждёт опознаваемое значение: получив пустое или
    // мусорное, она отвечает заглушкой «App not supported», и человек видит её
    // вместо серверов, ничего не понимая. Раньше на Windows без MachineGuid мы
    // слали "имя-компьютера-windows", а на Linux без machine-id — вообще ничего.
    QString hwidFromParts(const QStringList &parts) {
        const QByteArray device = QCryptographicHash::hash(parts.join('|').toUtf8(),
                                                           QCryptographicHash::Sha256);
        const QByteArray digest = QCryptographicHash::hash("fsnt-hwid-" + device.toHex(),
                                                           QCryptographicHash::Sha256);
        const QString hex = QString::fromLatin1(digest.toHex()).left(32).toUpper();
        // Формат тот же, что у остальных клиентов: UUID 8-4-4-4-12.
        return QStringLiteral("%1-%2-%3-%4-%5")
            .arg(hex.mid(0, 8), hex.mid(8, 4), hex.mid(12, 4), hex.mid(16, 4), hex.mid(20, 12));
    }

    bool looksLikeId(const QString &value) {
        static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F-]{16,}$"));
        return re.match(value).hasMatch();
    }

    // Идентификатор хранится в системе, а не только рядом с настройками:
    // на Windows это ветка реестра пользователя, на macOS — свой plist, на
    // Linux — файл в ~/.config. Папку клиента человек сносит вместе с
    // приложением, и тогда устройство выглядело бы для панели новым и занимало
    // ещё один слот. Файл рядом с настройками остаётся вторым местом: если
    // системную запись вычистит «чистильщик», идентификатор поднимется оттуда.
    constexpr auto kHwidKey = "device/hwid";

    // Не в папке программы: в переносном режиме это папка на флешке, а при
    // обычной установке она уезжает вместе с приложением — идентификатор обязан
    // пережить и то, и другое.
    QString hwidFilePath() {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("device_id"));
    }

    // На Windows пишем прямо в свою ветку реестра: там идентификатору и место,
    // и переустановка приложения его не трогает. На остальных системах опорой
    // служит файл в пользовательских данных — он тоже переживает переустановку,
    // а запись в общее хранилище настроек macOS иногда молча не доезжает.
    QSettings systemStore() {
#ifdef Q_OS_WIN
        return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\FSNT\\Throne"),
                         QSettings::NativeFormat);
#else
        return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("FSNT"),
                         QStringLiteral("Throne"));
#endif
    }

    QString systemStoredHwid() {
        QSettings store = systemStore();
        return store.value(QLatin1String(kHwidKey)).toString().trimmed();
    }

    void rememberEverywhere(const QString &hwid) {
        if (hwid.isEmpty()) return;
        QSettings store = systemStore();
        store.setValue(QLatin1String(kHwidKey), hwid);
        store.sync();

        QFile file(hwidFilePath());
        QDir().mkpath(QFileInfo(file).absolutePath());
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(hwid.toUtf8());
            file.close();
        }
    }

    QString rememberedHwid(const QString &primary, const QStringList &parts) {
        if (const QString fromSystem = systemStoredHwid(); !fromSystem.isEmpty()) {
            rememberEverywhere(fromSystem);   // на случай, если файл потеряли
            return fromSystem;
        }

        QFile file(hwidFilePath());
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString stored = QString::fromUtf8(file.readAll()).trimmed();
            file.close();
            if (!stored.isEmpty()) {
                rememberEverywhere(stored);
                return stored;
            }
        }

        // У кого система выдаёт свой идентификатор, тот остаётся со старым
        // значением: менять его на новое — это заново занять слот на панели.
        const QString chosen = looksLikeId(primary) ? primary : hwidFromParts(parts);
        rememberEverywhere(chosen);
        return chosen;
    }

    QStringList hwidParts(const QString &machineId) {
        return {machineId, QSysInfo::machineHostName(), QSysInfo::productType(),
                QSysInfo::currentCpuArchitecture(), qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"))};
    }
}

DeviceDetails GetDeviceDetails() {
    static const DeviceDetails details = []() {
        DeviceDetails d;

    #ifdef Q_OS_WIN
        const QString winMachineId = QString::fromUtf8(QSysInfo::machineUniqueId());
        d.hwid = rememberedHwid(winMachineId, hwidParts(winMachineId));

        d.os = QStringLiteral("Windows");

        VersionInfo info;
        WinVersion::GetVersion(info);
        d.osVersion = QString("%1.%2.%3").arg(info.Major).arg(info.Minor).arg(info.BuildNum);
        
        auto wm = winModel();
        auto wbb = winBaseBoard();
        d.model = (wm == wbb) ? wm : wm + "/" + wbb;
        if (d.hwid.isEmpty()) d.model = QSysInfo::prettyProductName();
    #elif defined(Q_OS_LINUX)
        QString mid;
        QFile f1("/etc/machine-id");
        if (f1.exists() && f1.open(QIODevice::ReadOnly | QIODevice::Text)) {
            mid = QString::fromUtf8(f1.readAll()).trimmed();
            f1.close();
        }
        else {
            QFile f2("/var/lib/dbus/machine-id");
            if (f2.exists() && f2.open(QIODevice::ReadOnly | QIODevice::Text)) {
                mid = QString::fromUtf8(f2.readAll()).trimmed();
                f2.close();
            }
        }
        d.hwid = rememberedHwid(mid, hwidParts(mid));
        d.os = QStringLiteral("Linux");
        d.osVersion = QSysInfo::kernelVersion();
        d.model = QSysInfo::prettyProductName();
    #elif defined(Q_OS_MACOS)
        const QString macMachineId = QString::fromUtf8(QSysInfo::machineUniqueId());
        d.hwid = rememberedHwid(macMachineId, hwidParts(macMachineId));
        d.os = QStringLiteral("macOS");
        d.osVersion = QSysInfo::productVersion();
        d.model = QSysInfo::prettyProductName();
    #else
        const QString otherMachineId = QString::fromUtf8(QSysInfo::machineUniqueId());
        d.hwid = rememberedHwid(otherMachineId, hwidParts(otherMachineId));
        d.os = QSysInfo::productType();
        d.osVersion = QSysInfo::productVersion();
        d.model = QSysInfo::prettyProductName();
    #endif
        return d;
    }();
    return details;
}