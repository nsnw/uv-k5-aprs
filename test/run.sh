#!/bin/sh
# Builds and runs the self-checking host tests with the system C compiler.
# Run from anywhere; needs cc and python3. test_aprs_parse and test_ax25
# print their results for inspection instead (see the top of each file).
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}

run() {   # name, extra flags, sources...
    name=$1 flags=$2; shift 2
    $CC $flags -I . -I src -o build/$name test/$name.c "$@"
    printf '%-14s ' "$name"
    out=$(build/$name) || { echo "$out"; echo "FAILED: $name"; exit 1; }
    echo "$out" | tail -n 1
}

run test_hdlc_tx ""                src/hdlc_tx.c src/ax25.c
run test_digi    "-DDIGI_HOST_TEST" src/digi.c src/ax25.c
run test_kiss    "-DENABLE_TX"      src/kiss.c src/txq.c
run test_menu    ""                src/menu.c src/memory.c src/textedit.c
run test_msg     "-DMSG_HOST_TEST"  src/msg.c
$CC -I . -I src -o build/test_beacon test/test_beacon.c src/beacon.c src/ax25.c
printf '%-14s ' test_beacon
out=$(python3 test/beacon_vectors.py) || { echo "$out"; echo "FAILED: test_beacon"; exit 1; }
echo "$out" | tail -n 1
