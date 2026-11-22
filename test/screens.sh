#!/bin/sh
# Screenshots of every main screen, rendered on the host from the firmware's
# own drawing code (see test/screens.c). Needs cc and python3; no firmware
# build needed. Output: build/screens/*.png and build/screens/index.html.
set -eu
cd "$(dirname "$0")/.."
OUT=build/screens
mkdir -p "$OUT"
CC=${CC:-cc}

# version.h as the Makefile writes it (the build as a tagged release shows it)
name=$(sed -n 's/^APP_NAME *:= *//p' Makefile)
version=$(sed -n 's/^VERSION *?= *//p' Makefile)
printf '#define APP_NAME "%s"\n#define VERSION_STRING "%s"\n#define BUILD_ID "v%s"\n' \
    "$name" "$version" "$version" > "$OUT/version.h"

$CC -DENABLE_TX -DMSG_HOST_TEST -DAPRS_HOST_TEST -I . -I src -I "$OUT" -o "$OUT/screens" \
    test/screens.c src/gfx.c src/font.c src/menu.c src/inbox.c src/textedit.c src/memory.c \
    src/msg.c src/aprs.c src/ax25.c src/aprs_parse.c src/geo.c
rm -f "$OUT"/*.fb "$OUT"/*.png
"$OUT/screens" "$OUT"
python3 test/screens_png.py "$OUT"
