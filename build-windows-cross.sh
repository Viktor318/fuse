#!/bin/bash
#
# build-windows-cross.sh: build a 64 bit Windows Fuse (native Win32 user
# interface) from Linux or WSL, using MinGW-w64
#
# Windows Fuse (natív Windows felület) fordítása Linuxról vagy WSL-ből
#
# Usage / Használat:
#   sudo apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
#        libz-mingw-w64-dev autoconf automake libtool pkg-config flex bison \
#        perl zip git
#   ./build-windows-cross.sh
#
# Result / Eredmény:  ~/fuse-windows/Fuse-windows-<branch>.zip, with
#   fuse.exe               WASAPI sound output (the default)
#   fuse-directsound.exe   DirectSound sound output (fallback / tartalék)
# FUSE_AUDIO_DRIVERS="wasapi" builds only one of them.
#
# It works on a copy of the source, so a native Linux build in this tree is
# not touched. libspectrum is fetched from GitHub and built as a static
# library with its internal GLib replacement.
# A forrás egy másolatán dolgozik, a Linuxos fordítást nem érinti.

set -e

HOST=x86_64-w64-mingw32
LIBSPECTRUM_REPO="${LIBSPECTRUM_REPO:-https://github.com/fuse-emulator/libspectrum.git}"

SRCDIR="$(cd "$(dirname "$0")" && pwd)"
WORK="${XDG_CACHE_HOME:-$HOME/.cache}/fuse-windows-build"
OUTDIR="${FUSE_WINDOWS_OUT:-$HOME/fuse-windows}"
PREFIX="$WORK/prefix"

BRANCH=$(git -C "$SRCDIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo build)
NAME="Fuse-windows-$BRANCH"

for program in $HOST-gcc autoreconf libtoolize pkg-config flex bison perl zip git; do
  if ! command -v $program >/dev/null 2>&1; then
    echo "Missing / Hiányzik: $program"
    echo "sudo apt install gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64" \
         "libz-mingw-w64-dev autoconf automake libtool pkg-config flex bison" \
         "perl zip git"
    exit 1
  fi
done

if [ ! -f "/usr/$HOST/include/zlib.h" ]; then
  echo "Missing / Hiányzik: libz-mingw-w64-dev"
  exit 1
fi

mkdir -p "$WORK" "$PREFIX/lib/pkgconfig"

# Only our own pkg-config files: never pick up Linux libraries
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH=

# libspectrum
if [ ! -d "$WORK/libspectrum" ]; then
  git clone --depth 1 "$LIBSPECTRUM_REPO" "$WORK/libspectrum"
else
  git -C "$WORK/libspectrum" pull --ff-only || true
fi

( cd "$WORK/libspectrum"
  [ -f configure ] || ./autogen.sh
  [ -f Makefile ] && make distclean >/dev/null 2>&1 || true
  ./configure --host=$HOST --prefix="$PREFIX" --with-fake-glib \
              --without-libgcrypt --without-bzip2 \
              --disable-shared --enable-static
  make -j"$(nproc)"
  make install )

# One build per sound output, each in a clean copy of Fuse
# Hangkimenetenként egy fordítás, mindegyik egy tiszta másolatban
build_variant() {
  driver=$1
  dir="$WORK/fuse-$driver"

  rm -rf "$dir"
  mkdir -p "$dir"
  ( cd "$SRCDIR" && git ls-files -z | xargs -0 tar -cf - ) | tar -xf - -C "$dir"

  ( cd "$dir"
    ./autogen.sh

    # libspectrum is static: don't import its symbols from a DLL
    CPPFLAGS="-DLIBSPECTRUM_EXPORTS" ./configure --host=$HOST --disable-sdl2 \
      --with-audio-driver="$driver" --without-png --without-libxml2

    # Some helper test programs don't link for Windows; fuse.exe is what we need
    make -k -j"$(nproc)" || true
    [ -f .libs/fuse.exe ] || { echo "fuse.exe ($driver) was not built"; exit 1; } )
}

DRIVERS="${FUSE_AUDIO_DRIVERS:-wasapi directsound}"
for driver in $DRIVERS; do
  build_variant "$driver"
done

# The files of the package come from the first build
cd "$WORK/fuse-${DRIVERS%% *}"

# Package / Csomag
rm -rf "$NAME"
mkdir -p "$NAME/roms" "$NAME/lib"
# fuse.exe: the first sound output; the others as fuse-<driver>.exe
first=1
for driver in $DRIVERS; do
  if [ $first = 1 ]; then
    cp "$WORK/fuse-$driver/.libs/fuse.exe" "$NAME/fuse.exe"
    first=0
  else
    cp "$WORK/fuse-$driver/.libs/fuse.exe" "$NAME/fuse-$driver.exe"
  fi
done
cp "/usr/$HOST/lib/zlib1.dll" "/usr/$HOST/lib/libwinpthread-1.dll" "$NAME/"
cp roms/*.rom roms/README.copyright "$NAME/roms/"
cp lib/*.bmp lib/*.png lib/*.jpg lib/*.scr "$NAME/lib/"
cp COPYING README AUTHORS "$NAME/"
# The descriptions of this edition / A kiadás leírásai
for doc in README.md README.hu.md CHANGELOG-fork.md CHANGELOG-fork.hu.md; do
  [ -f "$doc" ] && cp "$doc" "$NAME/"
done

mkdir -p "$OUTDIR"
rm -rf "$OUTDIR/$NAME" "$OUTDIR/$NAME.zip"
cp -a "$NAME" "$OUTDIR/"
zip -qr "$OUTDIR/$NAME.zip" "$NAME"

echo
echo "Windows build ready / Elkészült:"
for exe in "$OUTDIR/$NAME"/fuse*.exe; do echo "  $exe"; done
echo "  $OUTDIR/$NAME.zip"
