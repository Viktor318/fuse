#!/bin/sh
# Endpoint-independent i686 MinGW gate; does not configure or build Fuse.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$root/build/wasapi-toolchain"}
cc=${CC:-i686-w64-mingw32-gcc}
objdump=${OBJDUMP:-i686-w64-mingw32-objdump}
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
# Isolated focused-test config, never overwrite the configured project's file.
printf '%s\n' '/* Standalone native test configuration. */' > "$out/config.h"
"$cc" --version > "$out/toolchain.txt"
flags='-std=c11 -Wall -Wextra -Werror -O0 -DWINVER=0x0600 -D_WIN32_WINNT=0x0600'
# Intentional word splitting of fixed compiler flags.
"$cc" $flags -I"$out" "$root/unittests/wasapitoolchaintest.c" \
  -o "$out/wasapitoolchaintest.exe" -lole32 -lavrt -luuid
"$cc" $flags -I"$out" -I"$root" \
  "$root/unittests/win32audiowaittest.c" "$root/compat/win32/wait.c" \
  -o "$out/win32audiowaittest.exe"
"$cc" $flags -I"$out" -I"$root" \
  "$root/unittests/wasapipcmtest.c" "$root/sound/wasapi_pcm.c" \
  "$root/sound/wasapi_native.c" "$root/sound/sfifo.c" \
  -o "$out/wasapipcmtest.exe" -lole32 -lavrt -luuid -luser32
"$cc" $flags -I"$out" -I"$root" \
  "$root/unittests/wasapithreadtest.c" "$root/sound/wasapi_pcm.c" \
  "$root/sound/sfifo.c" -o "$out/wasapithreadtest.exe"
for test in wasapitoolchaintest win32audiowaittest wasapipcmtest wasapithreadtest; do
  "$objdump" -p "$out/$test.exe" > "$out/$test.imports.txt"
  # This checks the gate executables only, not future packaged dependencies.
  if grep -Ei 'DLL Name:.*(api-ms-|ext-ms-)' "$out/$test.imports.txt"; then
    echo 'Unexpected API-set import: inspect Vista compatibility' >&2
    exit 1
  fi
done
if [ "${WASAPI_RUN_NATIVE:-0}" = 1 ]; then
  "$out/wasapitoolchaintest.exe"
  "$out/win32audiowaittest.exe"
  "$out/wasapipcmtest.exe"
  "$out/wasapithreadtest.exe"
else
  echo 'Cross-link gate passed; Windows execution NOT performed.'
fi
