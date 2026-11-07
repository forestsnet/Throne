#include "include/configs/sub/ProfileSuccessor.hpp"

#include <QRegularExpression>

namespace Configs {
    QString ComparableProfileName(const QString &name) {
        static const QRegularExpression bracketed(R"([\(\[\{][^\)\]\}]*[\)\]\}])");
        QString stripped = name;
        stripped.remove(bracketed);
        QString result;
        for (const QChar c : stripped) {
            if (c.isLetterOrNumber()) result += c.toLower();
        }
        return result;
    }

    int PickSuccessor(const QString &name, const QString &type, const QList<SuccessorCandidate> &candidates) {
        if (candidates.isEmpty()) return -1;

        const QString comparable = ComparableProfileName(name);
        QList<int> exact;
        QList<int> similar;
        for (const auto &candidate : candidates) {
            if (candidate.type != type) continue;
            if (!name.isEmpty() && candidate.name == name) exact << candidate.id;
            if (!comparable.isEmpty() && ComparableProfileName(candidate.name) == comparable) similar << candidate.id;
        }
        if (exact.size() == 1) return exact.first();
        if (similar.size() == 1) return similar.first();
        return candidates.first().id;
    }
}
