#!/bin/sh
# Собирает DMG с оформлением: приложение слева, папка Applications справа,
# между ними стрелка на фоне.
#
# Оформление окна Finder хранит .DS_Store, заранее сделанный на маке с
# графической сессией (res/dmg/DS_Store). На сборочной машине окон нет, и
# управлять Finder через AppleScript там нельзя — поэтому готовый файл просто
# кладётся в образ.
#
# Использование: script/make_dmg.sh <путь к .app> <итоговый .dmg> [имя тома]
set -e

APP="$1"
OUT="$2"
VOLUME="${3:-FSNT Client}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -d "$APP" ]; then
    echo "no app bundle at $APP" >&2
    exit 1
fi

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

# Имя внутри образа фиксировано: позиции значков лежат в .DS_Store и привязаны
# к именам, а человеку в окне понятнее «FSNT Client», чем «Throne».
# ditto, а не cp: он переносит расширенные атрибуты и жёсткие ссылки бандла,
# без которых подпись приезжает битой. Символы из бандла вынул deploy_macos.sh:
# удалять что-либо из подписанного бандла нельзя, это срывает печать.
ditto "$APP" "$STAGING/FSNT Client.app"
# Скрывать расширение через SetFile нельзя: он вешает на бандл com.apple.FinderInfo,
# а подпись такого не терпит — codesign сообщает «resource fork, Finder information,
# or similar detritus not allowed», и образ уезжает с сорванной печатью. Finder и
# так прячет .app при настройках по умолчанию.
ln -s /Applications "$STAGING/Applications"

mkdir -p "$STAGING/.background"
cp "$ROOT/res/dmg/background.tiff" "$STAGING/.background/background.tiff"
if [ -f "$ROOT/res/dmg/DS_Store" ]; then
    cp "$ROOT/res/dmg/DS_Store" "$STAGING/.DS_Store"
fi

rm -f "$OUT"
hdiutil create -srcfolder "$STAGING" -volname "$VOLUME" -fs HFS+ \
    -format UDZO -imagekey zlib-level=9 -ov "$OUT" >/dev/null
echo "$OUT"
