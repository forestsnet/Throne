#!/bin/bash
# Подписывает бандл сертификатом разработчика. Без MACOS_SIGN_IDENTITY
# подписывает «для себя», как было раньше: ветки и форки собираются без секретов.
#
# Использование: script/sign_macos.sh <путь к .app>
set -euo pipefail

APP="$1"

if [[ -z "${MACOS_SIGN_IDENTITY:-}" ]]; then
    codesign --force --deep --sign - "$APP"
    exit 0
fi

# Расширенные атрибуты подпись не переносит: com.apple.FinderInfo или карантин,
# приехавшие с артефактом сборки, роняют codesign на «detritus not allowed».
xattr -cr "$APP"

# --deep Apple не рекомендует: вложенные бинарники он подписывает без hardened
# runtime, а нотаризация такое заворачивает. Подписываем сами, изнутри наружу.
sign() {
    codesign --force --timestamp --options runtime --sign "$MACOS_SIGN_IDENTITY" "$@"
}

# 1. Отдельные библиотеки и плагины. Всё, что лежит внутри .framework,
#    пропускаем: фреймворк подписывается целиком следующим шагом, и правка
#    внутренностей после этого сорвала бы его печать.
while IFS= read -r -d '' lib; do
    sign "$lib"
done < <(find "$APP/Contents" \( -name '*.dylib' -o -name '*.so' \) -type f \
             -not -path '*.framework/*' -print0)

# 2. Фреймворки — каждый как бандл.
for framework in "$APP/Contents/Frameworks"/*.framework; do
    [[ -d "$framework" ]] || continue
    sign "$framework"
done

# 3. Вспомогательные программы рядом с главной.
for helper in ThroneCore updater; do
    [[ -f "$APP/Contents/MacOS/$helper" ]] && sign "$APP/Contents/MacOS/$helper"
done

# 4. Сам бандл.
sign "$APP"

codesign --verify --deep --strict --verbose=2 "$APP"
echo "signed with: $MACOS_SIGN_IDENTITY"
