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
# not touched. libspectrum, libpng (PNG screenshots) and bzip2 (.bz2 files)
# are fetched from GitHub and built as static libraries, libspectrum with
# its internal GLib replacement; so only zlib1.dll and libwinpthread-1.dll
# go beside the exe.
# A forrás egy másolatán dolgozik, a Linuxos fordítást nem érinti.

set -e

HOST=x86_64-w64-mingw32
LIBSPECTRUM_REPO="${LIBSPECTRUM_REPO:-https://github.com/fuse-emulator/libspectrum.git}"
LIBPNG_REPO="${LIBPNG_REPO:-https://github.com/pnggroup/libpng.git}"
LIBPNG_VERSION="${LIBPNG_VERSION:-v1.6.59}"
BZIP2_REPO="${BZIP2_REPO:-https://github.com/libarchive/bzip2.git}"
BZIP2_VERSION="${BZIP2_VERSION:-bzip2-1.0.8}"

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

# zlib comes from the MinGW packages (zlib1.dll); tell pkg-config about it,
# as libpng refers to it
cat > "$PREFIX/lib/pkgconfig/zlib.pc" <<ZLIBPC
prefix=/usr/$HOST
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: zlib
Description: zlib compression library (MinGW package)
Version: 1.2
Cflags: -I\${includedir}
Libs: -L\${libdir} -lz
ZLIBPC

# bzip2: static, built once per version. Its header declares DLL function
# pointers on Windows unless BZ_EXPORT is defined, so everything using it
# is compiled with -DBZ_EXPORT
if [ ! -f "$PREFIX/.built-$BZIP2_VERSION" ]; then
  rm -rf "$WORK/bzip2"
  git clone --depth 1 --branch "$BZIP2_VERSION" "$BZIP2_REPO" "$WORK/bzip2"
  ( cd "$WORK/bzip2"
    make -j"$(nproc)" libbz2.a CC=$HOST-gcc AR=$HOST-ar RANLIB=$HOST-ranlib \
         CFLAGS="-O2 -DBZ_EXPORT -D_FILE_OFFSET_BITS=64"
    mkdir -p "$PREFIX/include" "$PREFIX/lib"
    cp bzlib.h "$PREFIX/include/"
    cp libbz2.a "$PREFIX/lib/" )
  touch "$PREFIX/.built-$BZIP2_VERSION"
fi

# libpng: static, built once per version
if [ ! -f "$PREFIX/.built-libpng-$LIBPNG_VERSION" ]; then
  rm -rf "$WORK/libpng"
  git clone --depth 1 --branch "$LIBPNG_VERSION" "$LIBPNG_REPO" "$WORK/libpng"
  ( cd "$WORK/libpng"
    [ -f configure ] || autoreconf -fi
    ./configure --host=$HOST --prefix="$PREFIX" \
                --disable-shared --enable-static
    make -j"$(nproc)"
    make install )
  touch "$PREFIX/.built-libpng-$LIBPNG_VERSION"
fi

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
              --without-libgcrypt --disable-shared --enable-static \
              CPPFLAGS="-I$PREFIX/include -DBZ_EXPORT" LDFLAGS="-L$PREFIX/lib"
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

    # libspectrum is static: don't import its symbols from a DLL; it needs
    # the static bzip2 too, and libpng zlib
    CPPFLAGS="-DLIBSPECTRUM_EXPORTS -DBZ_EXPORT" LDFLAGS="-L$PREFIX/lib" \
      LIBS="-lbz2 -lz" ./configure --host=$HOST --disable-sdl2 \
      --with-audio-driver="$driver" --without-libxml2

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
