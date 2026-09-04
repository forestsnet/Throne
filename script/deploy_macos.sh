#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy golang => .app ####
source "$(dirname "$0")/extract_core_artifact.sh"

mv deployment/$DEST_SUFFIX/* $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS

#### deploy qt & Dylib runtime => .app ####
pushd $GITHUB_WORKSPACE/build
macdeployqt Throne.app -verbose=3
popd

#### copy Qt/C++ updater to .app (macOS uses our updater, not Odin one) ####
if [ -f "$GITHUB_WORKSPACE/build/updater" ]; then
  echo "Copying Qt updater to .app bundle..."
  cp $GITHUB_WORKSPACE/build/updater $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS/updater
  chmod +x $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS/updater
  echo "Qt updater ready in .app bundle"
else
  echo "Warning: Qt updater not found at $GITHUB_WORKSPACE/build/updater"
fi

codesign --force --deep --sign - $GITHUB_WORKSPACE/build/Throne.app

dsymutil $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS/Throne
strip -S $GITHUB_WORKSPACE/build/Throne.app/Contents/MacOS/Throne

mv $GITHUB_WORKSPACE/build/Throne.app $DEST
