#!/bin/bash
set -e

source script/env_deploy.sh
if [[ $1 == 'arm64' ]]; then
  ARCH="arm64"
else
  if [[ $1 == 'x86_64' ]]; then
    ARCH="amd64"
  else
    ARCH="legacy-amd64"
  fi
fi
DEST=$DEPLOYMENT/macos-$ARCH

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
cd download-artifact
cd *darwin-$ARCH
tar xvzf artifacts.tgz -C ../../
cd ../..

mv $DEPLOYMENT/macos-$ARCH/* $BUILD/Throne.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $BUILD
macdeployqt Throne.app -verbose=3
popd

#### copy Qt/C++ updater to .app (macOS uses our updater, not Odin one) ####
if [ -f "$BUILD/updater" ]; then
  echo "Copying Qt updater to .app bundle..."
  cp $BUILD/updater $BUILD/Throne.app/Contents/MacOS/updater
  chmod +x $BUILD/Throne.app/Contents/MacOS/updater
  echo "Qt updater ready in .app bundle"
else
  echo "Warning: Qt updater not found at $BUILD/updater"
fi

codesign --force --deep --sign - $BUILD/Throne.app

dsymutil $BUILD/Throne.app/Contents/MacOS/Throne
strip -S $BUILD/Throne.app/Contents/MacOS/Throne

mv $BUILD/Throne.app $DEST
