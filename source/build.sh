#!/bin/sh
# Cross-compile with MinGW-w64 (Linux) or MSYS2 (Windows): produces GraphireDriver.exe
set -e
cd "$(dirname "$0")"
CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
mkdir -p build
$WINDRES -I res res/app.rc -O coff -o build/app.res
$CC -O2 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static -s \
    -o build/GraphireDriver.exe src/main.c src/device.c src/output.c src/graphire.c build/app.res \
    -lhid -lsetupapi -lshell32 -luser32 -lgdi32 -ladvapi32
echo "built build/GraphireDriver.exe"
