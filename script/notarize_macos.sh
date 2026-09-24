#!/bin/bash
# Отправляет бандл или образ на нотаризацию и прикрепляет талон. Без ключа
# выходит молча — как и подпись, нотаризация нужна только на сборках с секретами.
#
# Использование: script/notarize_macos.sh <путь к .app или .dmg>
set -euo pipefail

TARGET="$1"

if [[ -z "${MACOS_NOTARY_KEY:-}" ]]; then
    echo "notarization key not provided, skipping $TARGET"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

KEY="$WORK/key.p8"
printf '%s' "$MACOS_NOTARY_KEY" | base64 --decode > "$KEY"

# Бандл сам по себе отправить нельзя, нужен контейнер. ditto -c -k делает тот
# самый zip, который Apple принимает: с сохранением символьных ссылок и прав.
UPLOAD="$TARGET"
if [[ "$TARGET" == *.app ]]; then
    UPLOAD="$WORK/upload.zip"
    ditto -c -k --keepParent "$TARGET" "$UPLOAD"
fi

# Очередь Apple — не наша: первые заявки нового участника она перемалывает
# часами, дальше обычно минуты. Получасового запаса не хватило даже на первую,
# поэтому ждём столько, сколько задача вообще может себе позволить.
xcrun notarytool submit "$UPLOAD" \
    --key "$KEY" \
    --key-id "$MACOS_NOTARY_KEY_ID" \
    --issuer "$MACOS_NOTARY_ISSUER" \
    --wait --timeout 2h

# Талон кладём рядом с приложением. Без него первый запуск идёт за проверкой к
# Apple по сети, а у наших людей она до Apple добирается не всегда.
xcrun stapler staple "$TARGET"
xcrun stapler validate "$TARGET"
