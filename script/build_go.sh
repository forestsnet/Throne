#!/bin/bash
set -e

TAGS="with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_dhcp,with_tailscale,with_openvpn,with_openconnect,badlinkname,tfogo_checklinkname0"

rm -rf $DEST
mkdir -p $DEST

[[ "$GOOS" =~ legacy$ ]] && IS_LEGACY=true && GOCMD="$PWD/golang.org/go/bin/go" && GOOS="${GOOS%legacy}" || { IS_LEGACY=false; GOCMD="go"; }

if [[ "$GOOS" == "windows" || "$GOOS" == "linux" ]]; then
    # Odin остался только под Linux. macOS и Windows собирают свой обновлятор
    # через Qt/C++ (см. src/updater в CMakeLists.txt): Odin не дожидается
    # выхода приложения и на Windows падал с Permission_Denied.
    if [[ "$GOOS" == "linux" ]]; then
        curl -fLso "$DEST/updater" "https://github.com/throneproj/updater/releases/latest/download/updater-linux-$GOARCH"
        chmod +x "$DEST/updater"
    fi
fi

case "$GOOS" in
  windows)
    export CGO_ENABLED=0
    if ! $IS_LEGACY; then
      TAGS+=",with_purego,with_naive_outbound"
      curl -fLso $DEST/libcronet.dll "https://github.com/SagerNet/cronet-go/releases/latest/download/libcronet-windows-$GOARCH.dll"
    fi
    ;;
  darwin)
    TAGS+=",with_naive_outbound"
    export CGO_ENABLED=1 CGO_LDFLAGS="-weak_framework UniformTypeIdentifiers"
    ;;
  linux)
    TAGS+=",with_naive_outbound"
    export CGO_ENABLED=1
    ;;
esac

#### Go: core ####
pushd core/server
pushd gen
protoc -I . --go_out=. --go-grpc_out=. libcore.proto
popd
VERSION_SINGBOX=$(go list -m -f '{{.Version}}' github.com/sagernet/sing-box)
$GOCMD build -v -o $DEST -trimpath -ldflags "-w -s -X 'github.com/sagernet/sing-box/constant.Version=${VERSION_SINGBOX}' -X 'internal/godebug.defaultGODEBUG=multipathtcp=0' -checklinkname=0" -tags "$TAGS"
popd
