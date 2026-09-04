#!/bin/bash
set -e

rm -rf $DEST
mkdir -p $DEST

#### copy exe ####
cp $GITHUB_WORKSPACE/build/Throne.exe $DEST
cp $GITHUB_WORKSPACE/build/Throne.pdb $DEST || true

#### свой обновлятор вместо Odin: он дожидается выхода приложения ####
cp $GITHUB_WORKSPACE/build/updater.exe $DEST

source "$(dirname "$0")/extract_core_artifact.sh"
