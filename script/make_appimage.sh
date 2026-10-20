#!/bin/bash
# Собирает AppImage из готовой портативной сборки: один файл, который
# запускается на любом дистрибутиве, без пакетов и без установки.
#
# Использование: script/make_appimage.sh <каталог сборки> <итоговый .AppImage> [арх]
set -e

SOURCE="$1"
OUT="$2"
ARCH="${3:-x86_64}"

if [ ! -x "$SOURCE/Throne" ]; then
    echo "no Throne binary in $SOURCE" >&2
    exit 1
fi

# Собираем из копии: в исходном каталоге лежат отладочные символы, они уезжают
# в отдельный артефакт релиза, и удалять их оттуда нельзя. В образе же им не
# место — это десятки мегабайт, которые никто не скачивает осознанно.
APPDIR="$(mktemp -d)/AppDir"
mkdir -p "$APPDIR"
cp -a "$SOURCE/." "$APPDIR/"
find "$APPDIR" -name '*.debug' -delete

# AppRun запускает бинарник из корня AppDir: rpath у него '$ORIGIN/usr/lib',
# то есть библиотеки он ищет относительно себя, а не относительно usr/bin.
cat > "$APPDIR/AppRun" <<'LAUNCHER'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/Throne" "$@"
LAUNCHER
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/fsnt-client.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=FSNT Client
Comment=VPN client
Exec=Throne
Icon=Throne
Categories=Network;
Terminal=false
DESKTOP

# Значок окна AppImage берёт из .DirIcon.
cp "$APPDIR/Throne.png" "$APPDIR/.DirIcon"

TOOL="appimagetool-$ARCH.AppImage"
wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/$TOOL"
chmod +x "$TOOL"
# --appimage-extract-and-run: на сборочной машине нет FUSE, а сам инструмент
# тоже приезжает образом.
ARCH="$ARCH" "./$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT"
rm -f "$TOOL"
rm -rf "$(dirname "$APPDIR")"
echo "$OUT"
