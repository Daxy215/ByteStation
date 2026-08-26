#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/release_build"
EXE="$BUILD_DIR/src/PS1Emulator"

if [[ "$1" == "-f" ]]; then
    echo "Full build"

    rm -rf "$BUILD_DIR"
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

cmake --build "$BUILD_DIR" --config Release

echo "Running PS1Emulator..."
"$EXE"
