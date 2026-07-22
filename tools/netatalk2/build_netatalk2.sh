#!/usr/bin/env bash
# POM68K — hermetic, pinned build of netatalk 2.4.x (AFP-over-DDP server)
# and its two static dependencies. No system packages, no sudo: everything
# lands under extern/netatalk2/ (gitignored). Validated 2026-07-22 on
# Ubuntu 25.04 / kernel 6.14 (module `appletalk` present as .ko.zst).
#
# Result: extern/netatalk2/install/{sbin/afpd,sbin/atalkd,sbin/papd,...}
# Serve the repo's input/ folder to the emulated Macs through TashRouter —
# see tools/netatalk2/README.md and the main README "LocalTalk" section.
#
# Pins (change deliberately, update the checksums):
NETATALK_TAG=netatalk-2-4-9
BDB_URL=https://download.oracle.com/berkeley-db/db-5.3.28.tar.gz
BDB_SHA256=e0a992d740709892e81f9d93f06daf305cf73fb81b545afe72478043172c3628
GPGERR_URL=https://gnupg.org/ftp/gcrypt/libgpg-error/libgpg-error-1.47.tar.bz2
GPGERR_SHA256=9e3c670966b96ecc746c28c2c419541e3bcb787d1a73930f5e5f5e1bcbbb9bdb
GCRYPT_URL=https://gnupg.org/ftp/gcrypt/libgcrypt/libgcrypt-1.10.3.tar.bz2
GCRYPT_SHA256=8b0870897ac5ac67ded568dcfadf45969cfa8a6beb0fd60af2a9eadc2a3272aa

set -euo pipefail
cd "$(dirname "$0")/../.."                 # repo root
ROOT="$PWD/extern/netatalk2"
mkdir -p "$ROOT/src"
cd "$ROOT/src"

fetch() { # url sha256 out
    if [ ! -f "$3" ]; then curl -L --fail -o "$3" "$1"; fi
    echo "$2  $3" | sha256sum -c -
}

echo "── Berkeley DB 5.3 (static) ──"
fetch "$BDB_URL" "$BDB_SHA256" db.tar.gz
tar xzf db.tar.gz
( cd db-5.3.28/build_unix
  ../dist/configure --prefix="$ROOT/bdb" --enable-shared=no \
      --disable-java --disable-tcl \
      CFLAGS="-O2 -fPIC -Wno-implicit-int -Wno-implicit-function-declaration" \
      >/dev/null
  make -j"$(nproc)" -s && make install -s >/dev/null )

echo "── libgpg-error + libgcrypt (static, PIC) ──"
fetch "$GPGERR_URL" "$GPGERR_SHA256" gpg-error.tar.bz2
tar xjf gpg-error.tar.bz2
( cd libgpg-error-1.47
  ./configure --prefix="$ROOT/gcrypt" --enable-shared=no --with-pic -q
  make -j"$(nproc)" -s && make install -s >/dev/null )
fetch "$GCRYPT_URL" "$GCRYPT_SHA256" gcrypt.tar.bz2
tar xjf gcrypt.tar.bz2
( cd libgcrypt-1.10.3
  ./configure --prefix="$ROOT/gcrypt" --enable-shared=no --with-pic \
      --with-libgpg-error-prefix="$ROOT/gcrypt" -q
  make -j"$(nproc)" -s && make install -s >/dev/null )

echo "── netatalk $NETATALK_TAG ──"
if [ ! -d netatalk ]; then
    git clone --depth 1 --branch "$NETATALK_TAG" \
        https://github.com/Netatalk/netatalk netatalk
fi
( cd netatalk
  rm -rf b
  CFLAGS="-I$ROOT/gcrypt/include" LDFLAGS="-L$ROOT/gcrypt/lib" \
  meson setup b --prefix="$ROOT/install" \
      -Dwith-bdb-path="$ROOT/bdb" \
      -Dpkg_config_path="$ROOT/gcrypt/lib/pkgconfig" >/dev/null
  ninja -C b || ninja -C b        # first pass may race a generated header
  ninja -C b install >/dev/null )

echo
echo "OK: $ROOT/install/sbin/{afpd,atalkd,papd}"
"$ROOT/install/sbin/afpd" -v | head -1
"$ROOT/install/sbin/atalkd" -v 2>&1 | head -1
