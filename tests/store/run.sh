#!/bin/bash
# Unit tests for src/daemon/store.c (the print record), against a mock FpPrint.
# Runs anywhere with GLib (Linux CI or macOS):   bash tests/store/run.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../src"
OUT="$(mktemp -d)/test-store"
# shellcheck disable=SC2046  # pkg-config output must word-split
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration \
   -I"$HERE" -I"$SRC/common" -I"$SRC/daemon" \
   "$HERE/test-store.c" "$HERE/mock-fprint.c" "$SRC/daemon/store.c" \
   $(pkg-config --cflags --libs gio-2.0) -o "$OUT"
"$OUT"
