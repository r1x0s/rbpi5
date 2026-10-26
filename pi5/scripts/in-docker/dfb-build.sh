#!/bin/sh
# dfb-build.sh -- patched DirectFB 1.4.16, per tools/build-directfb/README.md.
# Runs in the build image (scripts/build.sh): /w = this checkout, /assets = the
# prepared assets. Output: /w/work/dfb. Source checkouts are cached in /w/work/cache.
set -eu
RX3ROOT=/assets/XDJRX3-rootfs
OUT=/w/work/dfb
mkdir -p /w/work/cache
cd /w/work/cache
[ -d flux ] || git clone -q https://github.com/deniskropp/flux.git flux
[ -d directfb ] || git clone -q https://github.com/deniskropp/DirectFB.git directfb
cd flux
git checkout -q 10ad2ebc78b396032714b839f200848ea0dd9503
[ -x src/fluxcomp ] || { autoreconf -fi >/dev/null 2>&1; CXX=g++ ./configure >/dev/null; make -j"$(nproc)" >/dev/null; }
export PATH="$PWD/src:$PATH"
cd ../directfb
git checkout -q -f 363739298335a0f76dea2de1c9bc523293242baa
git clean -qfdx
patch -s -p1 < /w/tools/build-directfb/directfb-full.diff
export CC=arm-linux-gnueabi-gcc
export CFLAGS="-march=armv5t -mfloat-abi=soft --sysroot=$RX3ROOT"
export LDFLAGS="--sysroot=$RX3ROOT -L$RX3ROOT/lib -L$RX3ROOT/usr/lib -Wl,-rpath-link,$RX3ROOT/lib:$RX3ROOT/usr/lib"
./autogen.sh --host=arm-linux-gnueabi --prefix=/usr --libdir=/lib \
    --disable-x11 --disable-sdl --disable-vnc --disable-avifile \
    --with-gfxdrivers=none --disable-osx --disable-devmem >/w/work/cache/configure.log 2>&1
make -j"$(nproc)" >/w/work/cache/make.log 2>&1
rm -rf "$OUT"; make install DESTDIR="$OUT" >/w/work/cache/install.log 2>&1
cd "$OUT"
for base in libdirect libfusion libdirectfb; do
  patchelf --set-soname "$base-1.4.so.0" "lib/$base-1.4.so.6.0.10"
  mv "lib/$base-1.4.so.6.0.10" "lib/$base-1.4.so.0.0.0"
  rm -f "lib/$base-1.4.so.6" "lib/$base-1.4.so"
  ln -sf "$base-1.4.so.0.0.0" "lib/$base-1.4.so.0"
done
for f in lib/lib*-1.4.so.0.0.0 lib/directfb-1.4-6/*/*.so; do
  for base in libdirect libfusion libdirectfb; do
    arm-linux-gnueabi-readelf -d "$f" | grep -q "\[$base-1.4.so.6\]" || continue
    patchelf --replace-needed "$base-1.4.so.6" "$base-1.4.so.0" "$f"
  done
done
echo "== versions"
n=0
for f in lib/lib*-1.4.so.0.0.0 lib/directfb-1.4-6/*/*.so; do
  n=$((n+1))
  printf '%-46s %s\n' "$(basename "$f")" "$(arm-linux-gnueabi-objdump -T "$f" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tr '\n' ' ')"
done
echo "objects: $n"
echo "== unversioned"
for f in lib/lib*-1.4.so.0.0.0 lib/directfb-1.4-6/*/*.so; do
  bad=$(arm-linux-gnueabi-nm -D --undefined-only "$f" | grep -E ' (fstat|__fdelt_chk|__xstat|stat|open|mmap|fcntl)$' || true)
  [ -n "$bad" ] && printf '  %-32s %s\n' "$(basename "$f")" "$(echo $bad)"
done
echo "== sonames"
for f in lib/libdirect*-1.4.so.0.0.0 lib/libfusion-1.4.so.0.0.0 lib/directfb-1.4-6/*/*.so; do
  printf '%-30s %s\n' "$(basename "$f")" "$(arm-linux-gnueabi-readelf -d "$f" | grep -E 'SONAME|NEEDED.*(direct|fusion)' | tr -s ' ' | tr '\n' ' ')"
done
