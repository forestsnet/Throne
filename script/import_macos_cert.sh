#!/bin/bash
# Кладёт сертификат разработчика во временную связку ключей сборочной машины.
# Без секрета выходит молча: так собираются ветки и форки без доступа к нему.
set -euo pipefail

if [[ -z "${MACOS_CERT_P12:-}" ]]; then
    echo "signing certificate not provided, the build will be signed ad-hoc"
    exit 0
fi

KEYCHAIN="$RUNNER_TEMP/signing.keychain-db"
KEYCHAIN_PASSWORD="$(uuidgen)"
CERT="$RUNNER_TEMP/certificate.p12"
trap 'rm -f "$CERT"' EXIT

printf '%s' "$MACOS_CERT_P12" | base64 --decode > "$CERT"

security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
security import "$CERT" -k "$KEYCHAIN" -P "${MACOS_CERT_PASSWORD:-}" \
    -T /usr/bin/codesign -T /usr/bin/security
# Без этого codesign упирается в диалог подтверждения, которого на сборочной
# машине некому нажать, и просто зависает до таймаута задачи.
security set-key-partition-list -S apple-tool:,apple:,codesign: \
    -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')

echo "imported signing identities:"
security find-identity -v -p codesigning "$KEYCHAIN"
