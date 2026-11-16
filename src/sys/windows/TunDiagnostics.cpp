#include "include/sys/TunDiagnostics.h"

#include <QFileInfo>
#include <QObject>

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wintrust.h>
#include <softpub.h>

namespace Sys::WinTunDiag {
    namespace {
        // {4d36e972-e325-11ce-bfc1-08002be10318} — класс Net. Объявляем сами:
        // в devguid.h это DEFINE_GUID, и за символом пришлось бы тянуть uuid.lib.
        constexpr GUID kNetClassGuid = {0x4d36e972, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

        constexpr auto kWintunService = L"Wintun";

        constexpr int kPropertyChars = 512;

        QString regProperty(HDEVINFO set, SP_DEVINFO_DATA *data, DWORD property) {
            wchar_t buffer[kPropertyChars] = {};
            DWORD type = 0;
            if (!SetupDiGetDeviceRegistryPropertyW(set, data, property, &type, reinterpret_cast<PBYTE>(buffer), sizeof(buffer), nullptr)) {
                return {};
            }
            if (type != REG_SZ && type != REG_EXPAND_SZ) return {};
            buffer[kPropertyChars - 1] = L'\0';
            return QString::fromWCharArray(buffer);
        }

        // Путь службы драйвера приходит в форме ядра. QFileInfo про такие ничего
        // не знает, а нам надо ответить на один вопрос: файл на месте или нет.
        QString resolveDriverPath(QString path) {
            if (path.isEmpty()) return path;
            wchar_t windows[MAX_PATH] = {};
            const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
            const QString root = length > 0 ? QString::fromWCharArray(windows, static_cast<int>(length)) : QStringLiteral("C:\\Windows");
            if (path.startsWith(QLatin1String("\\??\\"), Qt::CaseInsensitive)) path = path.mid(4);
            if (path.startsWith(QLatin1String("\\SystemRoot\\"), Qt::CaseInsensitive)) return root + QLatin1Char('\\') + path.mid(12);
            if (path.startsWith(QLatin1String("system32\\"), Qt::CaseInsensitive)) return root + QLatin1Char('\\') + path;
            if (path.startsWith(QLatin1String("\\"))) return root + path;
            return path;
        }

        QString fileVersion(const QString &path) {
            const auto native = path.toStdWString();
            DWORD handle = 0;
            const DWORD size = GetFileVersionInfoSizeW(native.c_str(), &handle);
            if (size == 0) return {};
            QByteArray block(static_cast<int>(size), Qt::Uninitialized);
            if (!GetFileVersionInfoW(native.c_str(), handle, size, block.data())) return {};
            VS_FIXEDFILEINFO *fixed = nullptr;
            UINT length = 0;
            if (!VerQueryValueW(block.constData(), L"\\", reinterpret_cast<LPVOID *>(&fixed), &length) || fixed == nullptr) return {};
            return QStringLiteral("%1.%2.%3.%4")
                .arg(HIWORD(fixed->dwFileVersionMS))
                .arg(LOWORD(fixed->dwFileVersionMS))
                .arg(HIWORD(fixed->dwFileVersionLS))
                .arg(LOWORD(fixed->dwFileVersionLS));
        }

        // Антивирусы режут файл ядра, а драйвер правят ещё охотнее: обрезанный
        // или подменённый wintun.sys даёт ровно ту же ошибку, что отсутствующий.
        void checkSignature(const QString &path, int *result, QString *note) {
            *result = 0;
            const auto native = path.toStdWString();
            WINTRUST_FILE_INFO file = {};
            file.cbStruct = sizeof(file);
            file.pcwszFilePath = native.c_str();
            WINTRUST_DATA data = {};
            data.cbStruct = sizeof(data);
            data.dwUIChoice = WTD_UI_NONE;
            data.fdwRevocationChecks = WTD_REVOKE_NONE;
            data.dwUnionChoice = WTD_CHOICE_FILE;
            data.pFile = &file;
            data.dwStateAction = WTD_STATEACTION_VERIFY;
            // Без обращений к сети: проверка идёт перед показом окна об ошибке, а
            // сеть в этот момент как раз и не работает.
            data.dwProvFlags = WTD_SAFER_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL;
            GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            const LONG verdict = WinVerifyTrust(nullptr, &action, &data);
            data.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(nullptr, &action, &data);
            if (verdict == 0) {
                *result = 1;
                *note = QStringLiteral("valid");
                return;
            }
            // Драйверы чаще подписаны каталогом, а не вложенной подписью, и
            // отсутствие второй ни о чём не говорит. Объявить такой файл
            // повреждённым значило бы пугать половину машин без причины.
            if (verdict == static_cast<LONG>(TRUST_E_NOSIGNATURE) || verdict == static_cast<LONG>(TRUST_E_SUBJECT_FORM_UNKNOWN) ||
                verdict == static_cast<LONG>(TRUST_E_PROVIDER_UNKNOWN)) {
                *note = QStringLiteral("no embedded signature (a catalog signature is not checked here)");
                return;
            }
            *result = -1;
            *note = QStringLiteral("failed, 0x%1").arg(static_cast<quint32>(verdict), 8, 16, QLatin1Char('0'));
        }

        bool looksLikeOurs(const NetDevice &device) {
            return device.service.compare(QLatin1String("Wintun"), Qt::CaseInsensitive) == 0 ||
                   device.instanceId.startsWith(QLatin1String("SWD\\Wintun"), Qt::CaseInsensitive);
        }

        QString describe(const NetDevice &device) {
            const QString name = device.description.isEmpty() ? QStringLiteral("(unnamed)") : device.description;
            return QStringLiteral("%1 [%2] service=%3 problem=%4 (%5)")
                .arg(name, device.instanceId, device.service.isEmpty() ? QStringLiteral("none") : device.service)
                .arg(device.problem)
                .arg(ProblemText(device.problem));
        }

        // Драйверы и службы, которые встают в сетевой стек рядом с нами. Имена
        // точные и сверяются целиком: подстрока ловила бы чужое.
        const wchar_t *const kNetFilterServices[] = {
            // WinDivert — под ним работают GoodbyeDPI, zapret, ByeDPI и прочие.
            L"WinDivert", L"WinDivert1.4", L"WinDivert14",
            // Драйверы TAP и виртуальных адаптеров.
            L"tap0901", L"tap0801", L"tapoas", L"VMnetAdapter", L"VMnetBridge", L"vmnetadapter",
            // Netfilter SDK: основа десятка «родительских контролей» и фильтров.
            L"netfilter2", L"nfapi", L"netfilter",
            // Свои туннели.
            L"ZeroTierOne", L"CloudflareWARP", L"Cloudflare WARP", L"ovpn-dco", L"WireGuardManager", L"wgipv4", L"Amnezia-WG",
            // Сетевые фильтры антивирусов.
            L"klwfp", L"kltdi", L"klim6", L"kltap", L"epfwwfp", L"ehdrv", L"avgntflt", L"aswNetSec", L"aswSP", L"nvcwfpdrv",
        };
    } // namespace

    QList<NetDevice> NetDevices() {
        QList<NetDevice> devices;
        const HDEVINFO set = SetupDiGetClassDevsW(&kNetClassGuid, nullptr, nullptr, DIGCF_PRESENT);
        if (set == INVALID_HANDLE_VALUE) return devices;
        SP_DEVINFO_DATA data = {};
        data.cbSize = sizeof(data);
        for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &data); ++index) {
            NetDevice device;
            wchar_t instance[MAX_DEVICE_ID_LEN] = {};
            if (SetupDiGetDeviceInstanceIdW(set, &data, instance, MAX_DEVICE_ID_LEN, nullptr)) {
                device.instanceId = QString::fromWCharArray(instance);
            }
            device.description = regProperty(set, &data, SPDRP_FRIENDLYNAME);
            if (device.description.isEmpty()) device.description = regProperty(set, &data, SPDRP_DEVICEDESC);
            device.service = regProperty(set, &data, SPDRP_SERVICE);
            ULONG status = 0;
            ULONG problem = 0;
            if (CM_Get_DevNode_Status(&status, &problem, data.DevInst, 0) == CR_SUCCESS) {
                device.status = status;
                device.problem = problem;
            }
            devices.append(device);
        }
        SetupDiDestroyDeviceInfoList(set);
        return devices;
    }

    DriverFile WintunDriver(const bool verify) {
        DriverFile driver;
        const SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm == nullptr) return driver;
        const SC_HANDLE service = OpenServiceW(scm, kWintunService, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
        if (service == nullptr) {
            CloseServiceHandle(scm);
            return driver;
        }
        driver.serviceInstalled = true;
        SERVICE_STATUS status = {};
        if (QueryServiceStatus(service, &status)) driver.serviceState = status.dwCurrentState;
        DWORD needed = 0;
        QueryServiceConfigW(service, nullptr, 0, &needed);
        if (needed > 0) {
            QByteArray buffer(static_cast<int>(needed), Qt::Uninitialized);
            auto *config = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buffer.data());
            if (QueryServiceConfigW(service, config, needed, &needed) && config->lpBinaryPathName != nullptr) {
                driver.binaryPath = resolveDriverPath(QString::fromWCharArray(config->lpBinaryPathName));
            }
        }
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        if (driver.binaryPath.isEmpty()) return driver;
        const QFileInfo info(driver.binaryPath);
        driver.fileExists = info.exists() && info.isFile();
        if (!driver.fileExists) return driver;
        driver.size = info.size();
        driver.version = fileVersion(driver.binaryPath);
        if (verify) checkSignature(driver.binaryPath, &driver.signature, &driver.signatureNote);
        return driver;
    }

    QStringList RunningNetFilters() {
        QStringList found;
        const SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
        if (scm == nullptr) return found;
        DWORD needed = 0;
        DWORD returned = 0;
        DWORD resume = 0;
        EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_WIN32, SERVICE_ACTIVE, nullptr, 0, &needed, &returned,
                              &resume, nullptr);
        if (needed > 0) {
            QByteArray buffer(static_cast<int>(needed), Qt::Uninitialized);
            auto *services = reinterpret_cast<LPENUM_SERVICE_STATUS_PROCESSW>(buffer.data());
            resume = 0;
            if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER | SERVICE_WIN32, SERVICE_ACTIVE,
                                      reinterpret_cast<LPBYTE>(services), needed, &needed, &returned, &resume, nullptr)) {
                for (DWORD index = 0; index < returned; ++index) {
                    const auto &entry = services[index];
                    if (entry.lpServiceName == nullptr) continue;
                    const QString name = QString::fromWCharArray(entry.lpServiceName);
                    for (const wchar_t *known : kNetFilterServices) {
                        if (name.compare(QString::fromWCharArray(known), Qt::CaseInsensitive) != 0) continue;
                        const QString display =
                            entry.lpDisplayName != nullptr ? QString::fromWCharArray(entry.lpDisplayName) : QString();
                        const bool isDriver = (entry.ServiceStatusProcess.dwServiceType & SERVICE_DRIVER) != 0;
                        found.append(display.isEmpty() || display == name
                                         ? QStringLiteral("%1 (%2)").arg(name, isDriver ? QStringLiteral("driver") : QStringLiteral("service"))
                                         : QStringLiteral("%1 — %2 (%3)")
                                               .arg(display, name, isDriver ? QStringLiteral("driver") : QStringLiteral("service")));
                        break;
                    }
                }
            }
        }
        CloseServiceHandle(scm);
        return found;
    }

    Diagnosis Diagnose(const bool verifySignature) {
        Diagnosis out;
        const auto devices = NetDevices();
        const auto driver = WintunDriver(verifySignature);

        QStringList broken;
        QStringList classStuck;
        QStringList ourStuck;
        quint32 ourProblem = 0;
        for (const auto &device : devices) {
            if (device.problem == 0) continue;
            // Отключённое вручную устройство — это выбор человека, а не поломка.
            if (device.problem == kProblemDisabled && !looksLikeOurs(device)) continue;
            broken.append(describe(device));
            if (device.problem == kProblemNeedClassConfig) {
                classStuck.append(device.description.isEmpty() ? device.instanceId : device.description);
            }
            if (looksLikeOurs(device)) {
                ourStuck.append(describe(device));
                if (ourProblem == 0) ourProblem = device.problem;
            }
        }

        QStringList details;
        details.append(QStringLiteral("Net class devices: %1 present, %2 with a problem").arg(devices.size()).arg(broken.size()));
        for (const QString &line : broken) details.append(QStringLiteral("  ! ") + line);
        if (driver.serviceInstalled) {
            details.append(QStringLiteral("Wintun driver service: state=%1 path=%2 %3")
                               .arg(driver.serviceState)
                               .arg(driver.binaryPath.isEmpty() ? QStringLiteral("(unknown)") : driver.binaryPath,
                                    driver.fileExists ? QStringLiteral("present") : QStringLiteral("MISSING")));
            if (driver.fileExists) {
                details.append(QStringLiteral("Wintun driver file: %1 bytes, version %2, signature %3")
                                   .arg(driver.size)
                                   .arg(driver.version.isEmpty() ? QStringLiteral("(none)") : driver.version,
                                        driver.signatureNote.isEmpty() ? QStringLiteral("(not checked)") : driver.signatureNote));
            }
        } else {
            details.append(QStringLiteral("Wintun driver service: not installed yet (the core installs it on the first run)"));
        }
        const auto filters = RunningNetFilters();
        if (!filters.isEmpty()) details.append(QStringLiteral("Running network filters: ") + filters.join(QStringLiteral(", ")));
        out.details = details.join(QLatin1Char('\n'));

        // Порядок важен: сломанная настройка класса объясняет и застрявшее
        // устройство, и отсутствие драйвера, и называть надо её.
        const auto settle = [&out](const Verdict verdict, const QString &first = {}, const QString &second = {}) {
            out.verdict = verdict;
            out.headline = HeadlineFor(verdict, first, second);
            out.blocksStart = BlocksStart(verdict);
        };
        if (!classStuck.isEmpty()) {
            settle(Verdict::ClassConfigBroken);
            return out;
        }
        if (!ourStuck.isEmpty()) {
            const QString code = QString::number(ourProblem);
            if (ourProblem == kProblemNeedRestart) {
                settle(Verdict::NeedsRestart, code);
            } else if (ourProblem == kProblemDisabled) {
                settle(Verdict::DeviceDisabled, code);
            } else if (ourProblem == kProblemFailedPriorUnload || ourProblem == kProblemFailedLoad) {
                settle(Verdict::DeviceUnloading, code);
            } else {
                settle(Verdict::DeviceLeftover, code);
            }
            return out;
        }
        if (driver.serviceInstalled && !driver.binaryPath.isEmpty() && !driver.fileExists) {
            settle(Verdict::DriverMissing, driver.binaryPath);
            return out;
        }
        if (driver.signature < 0) {
            settle(Verdict::DriverDamaged, driver.binaryPath, driver.signatureNote);
            return out;
        }
        return out;
    }

    QString DevicesReport() {
        QStringList out;
        const auto devices = NetDevices();
        out.append(QStringLiteral("=== NET CLASS DEVICES (%1) ===").arg(devices.size()));
        for (const auto &device : devices) {
            out.append(QStringLiteral("%1%2").arg(device.problem == 0 ? QStringLiteral("  ") : QStringLiteral("! "), describe(device)));
        }
        const auto driver = WintunDriver(true);
        out.append(QString());
        out.append(QStringLiteral("=== TUNNEL DRIVER ==="));
        if (!driver.serviceInstalled) {
            out.append(QStringLiteral("Wintun service: not installed"));
        } else {
            out.append(QStringLiteral("Wintun service state: %1").arg(driver.serviceState));
            out.append(QStringLiteral("Wintun image path: %1").arg(driver.binaryPath));
            out.append(QStringLiteral("Wintun file present: %1").arg(driver.fileExists ? QStringLiteral("yes") : QStringLiteral("NO")));
            if (driver.fileExists) {
                out.append(QStringLiteral("Wintun file size: %1").arg(driver.size));
                out.append(QStringLiteral("Wintun file version: %1").arg(driver.version.isEmpty() ? QStringLiteral("(none)") : driver.version));
                out.append(QStringLiteral("Wintun signature: %1").arg(driver.signatureNote));
            }
        }
        out.append(QString());
        out.append(QStringLiteral("=== RUNNING NETWORK FILTERS ==="));
        const auto filters = RunningNetFilters();
        if (filters.isEmpty()) {
            out.append(QStringLiteral("none of the known ones"));
        } else {
            for (const QString &filter : filters) out.append(filter);
        }
        return out.join(QLatin1Char('\n'));
    }
} // namespace Sys::WinTunDiag
