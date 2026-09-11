#!/bin/bash
# Собирает AppImage из готовой портативной сборки: один файл, который
# запускается на любом дистрибутиве, без пакетов и без установки.
#
# Использование: script/make_appimage.sh <каталог сборки> <итоговый .AppImage> [арх]
set -e

APPDIR="$1"
OUT="$2"
ARCH="${3:-x86_64}"

if [ ! -x "$APPDIR/Throne" ]; then
    echo "no Throne binary in $APPDIR" >&2
    exit 1
fi

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
echo "$OUT"
