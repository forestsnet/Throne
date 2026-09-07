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
cp -R "$APP" "$STAGING/FSNT Client.app"
# Отладочные символы в образ не кладём: они удваивают его вес и людям не нужны.
rm -rf "$STAGING/FSNT Client.app/Contents/MacOS/"*.dSYM
# Расширение в имени человеку ничего не даёт, а подпись под значком удлиняет.
SetFile -a E "$STAGING/FSNT Client.app" 2>/dev/null || true
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
