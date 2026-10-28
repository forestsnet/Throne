#include "include/engine/services/GeoAssetMirrors.h"

#include <QUrl>

namespace Engine {
    namespace {
        struct GitHubFile {
            QString owner;
            QString repo;
            QString ref;
            QString path;
            bool valid = false;
        };

        // Два написания одного и того же файла, которые встречаются в настройках:
        //   https://github.com/<owner>/<repo>/raw/<ref>/<path>
        //   https://raw.githubusercontent.com/<owner>/<repo>/<ref>/<path>
        GitHubFile parseGitHubFile(const QString &url) {
            const QUrl parsed(url);
            if (parsed.scheme() != QLatin1String("https")) return {};

            const auto host = parsed.host();
            auto parts = parsed.path().split('/', Qt::SkipEmptyParts);

            if (host == QLatin1String("github.com")) {
                // owner, repo, "raw", ref, затем путь внутри репозитория
                if (parts.size() < 5 || parts.at(2) != QLatin1String("raw")) return {};
                GitHubFile file;
                file.owner = parts.at(0);
                file.repo = parts.at(1);
                file.ref = parts.at(3);
                file.path = QStringList(parts.mid(4)).join('/');
                file.valid = true;
                return file;
            }

            if (host == QLatin1String("raw.githubusercontent.com")) {
                // owner, repo, ref, затем путь внутри репозитория
                if (parts.size() < 4) return {};
                GitHubFile file;
                file.owner = parts.at(0);
                file.repo = parts.at(1);
                file.ref = parts.at(2);
                file.path = QStringList(parts.mid(3)).join('/');
                file.valid = true;
                return file;
            }

            return {};
        }
    } // namespace

    QStringList GeoAssetMirrors(const QString &url) {
        const auto trimmed = url.trimmed();
        if (trimmed.isEmpty()) return {};

        QStringList candidates{trimmed};
        const auto file = parseGitHubFile(trimmed);
        if (!file.valid) return candidates;

        const auto add = [&candidates](const QString &candidate) {
            if (!candidates.contains(candidate)) candidates << candidate;
        };

        // jsDelivr отдаёт содержимое репозитория со своей сети доставки и доезжает
        // там, где GitHub недоступен.
        add(QStringLiteral("https://cdn.jsdelivr.net/gh/%1/%2@%3/%4")
                .arg(file.owner, file.repo, file.ref, file.path));
        // Прямая раздача GitHub: другое имя хоста и другие адреса, чем у github.com.
        add(QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3/%4")
                .arg(file.owner, file.repo, file.ref, file.path));

        return candidates;
    }
} // namespace Engine
