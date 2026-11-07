#include <QtTest>

#include "include/configs/sub/ProfileSuccessor.hpp"

using Configs::ComparableProfileName;
using Configs::PickSuccessor;
using Configs::SuccessorCandidate;

class ProfileSuccessorTest : public QObject {
    Q_OBJECT

    static SuccessorCandidate at(int id, const QString &name, const QString &type = QStringLiteral("custom")) {
        return {id, name, type};
    }

private slots:
    void stripsFlagsEmojiAndTags() {
        QCOMPARE(ComparableProfileName(QStringLiteral("🇫🇮 Финляндия [25 Gbps]")), QStringLiteral("финляндия"));
        QCOMPARE(ComparableProfileName(QStringLiteral("🇫🇮 Финляндия [20 Gbps]")), QStringLiteral("финляндия"));
        QCOMPARE(ComparableProfileName(QStringLiteral("🌎 Автоматически [YT] | (DEV)")), QStringLiteral("автоматически"));
        // Цифры в самом имени, а не в пометке, остаются: иначе LTE [1] и LTE [2]
        // схлопнулись бы в один сервер, а это разные узлы.
        QCOMPARE(ComparableProfileName(QStringLiteral("🇫🇮 LTE 7")), QStringLiteral("lte7"));
    }

    // Ровно тот случай, ради которого всё затевалось: обновление принесло тот
    // же сервер под тем же именем, а старый дожил до отключения.
    void exactNameWins() {
        const QList<SuccessorCandidate> candidates{
            at(937, QStringLiteral("🌎 Автоматически [YT] | (DEV)")),
            at(938, QStringLiteral("🇳🇱 Нидерланды [🔥] [10 Gbps]")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🌎 Автоматически [YT] | (DEV)"), QStringLiteral("custom"), candidates), 937);
    }

    // Поставщик переписал скорость в названии. Один в один не совпадёт, а для
    // человека это тот же узел.
    void tagChangedStillMatches() {
        const QList<SuccessorCandidate> candidates{
            at(939, QStringLiteral("🇩🇪 Германия  [🔥] [25 Gbps]")),
            at(941, QStringLiteral("🇫🇮 Финляндия [25 Gbps]")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🇫🇮 Финляндия [20 Gbps]"), QStringLiteral("custom"), candidates), 941);
    }

    // Два кандидата с одинаковым именем без пометок — угадывать не из чего,
    // поэтому берём первый, а не бросаем монетку между похожими.
    void ambiguousFallsBackToFirst() {
        const QList<SuccessorCandidate> candidates{
            at(937, QStringLiteral("🌎 Автоматически [YT] | (DEV)")),
            at(941, QStringLiteral("🇫🇮 Финляндия [25 Gbps]")),
            at(942, QStringLiteral("🇫🇮 Финляндия [Резерв]")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🇫🇮 Финляндия [10 Gbps]"), QStringLiteral("custom"), candidates), 937);
    }

    // Точное совпадение сильнее неоднозначности: резерв рядом не мешает.
    void exactBeatsAmbiguousSimilar() {
        const QList<SuccessorCandidate> candidates{
            at(937, QStringLiteral("🌎 Автоматически [YT] | (DEV)")),
            at(941, QStringLiteral("🇫🇮 Финляндия [25 Gbps]")),
            at(942, QStringLiteral("🇫🇮 Финляндия [Резерв]")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🇫🇮 Финляндия [25 Gbps]"), QStringLiteral("custom"), candidates), 941);
    }

    // Имя сравниваем только внутри своего типа. Совпавший по названию чужой
    // транспорт — не замена, и тогда остаётся первый в списке.
    void otherTypesDoNotMatch() {
        const QList<SuccessorCandidate> candidates{
            at(274, QStringLiteral("px-portDxt7b6f369"), QStringLiteral("xrayvless")),
            at(941, QStringLiteral("🇫🇮 Финляндия [25 Gbps]"), QStringLiteral("xrayvless")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🇫🇮 Финляндия [25 Gbps]"), QStringLiteral("custom"), candidates), 274);
    }

    // Несколько одинаковых имён — тот же дубль, только с другой стороны. Выбора
    // снова нет, берём первый.
    void severalExactMatchesFallBack() {
        const QList<SuccessorCandidate> candidates{
            at(900, QStringLiteral("🇺🇸 США")),
            at(944, QStringLiteral("🇷🇺 Россия")),
            at(945, QStringLiteral("🇷🇺 Россия")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🇷🇺 Россия"), QStringLiteral("custom"), candidates), 900);
    }

    void emptyGroupHasNoSuccessor() {
        QCOMPARE(PickSuccessor(QStringLiteral("🇺🇸 США"), QStringLiteral("custom"), {}), -1);
    }

    // Имя без букв вообще — одни эмодзи. Сравнивать нечем, поэтому сразу первый,
    // а не случайный сосед, у которого букв тоже нет.
    void namelessProfileTakesTheFirst() {
        const QList<SuccessorCandidate> candidates{
            at(944, QStringLiteral("🇺🇸 США")),
            at(945, QStringLiteral("🔥🔥")),
        };
        QCOMPARE(PickSuccessor(QStringLiteral("🌎"), QStringLiteral("custom"), candidates), 944);
    }
};

QTEST_MAIN(ProfileSuccessorTest)

#include "ProfileSuccessorTest.moc"
