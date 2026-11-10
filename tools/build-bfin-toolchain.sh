#!/usr/bin/env bash
# Build a Blackfin cross compiler (bfin-elf-gcc 13.3.0 + binutils 2.42 + libgcc) for hundekraft plugins.
# Linux (or a Linux VM/container on a Mac). Needs gcc, make, curl, xz. Takes 30 to 60 minutes, once.
# Duck Call's compiler was built with these same configure options; this script uses the official GNU
# release tarballs, which include generated files, so it needs no flex or bison. If a step stops, run it again.
set -euo pipefail
PREFIX=${PREFIX:-$HOME/bfin/tc}; T=bfin-elf; J=$(nproc 2>/dev/null || echo 4)
W=${W:-$HOME/bfin/build}; mkdir -p "$W" && cd "$W"
export PATH=$PREFIX/bin:$PATH MAKEINFO=true
[ -s binutils-2.42.tar.xz ] || curl -fLO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
[ -s gcc-13.3.0.tar.xz ]    || curl -fLO https://ftp.gnu.org/gnu/gcc/gcc-13.3.0/gcc-13.3.0.tar.xz
if [ ! -x "$PREFIX/bin/$T-ld" ]; then
  rm -rf binutils-2.42 b-bin && tar xf binutils-2.42.tar.xz && mkdir b-bin && cd b-bin
  ../binutils-2.42/configure --target=$T --prefix="$PREFIX" --disable-nls --disable-werror \
    --disable-gdb --disable-sim --disable-gdbserver --disable-gprofng
  make -j"$J" && make install && cd "$W"
fi
if [ ! -x "$PREFIX/bin/$T-gcc" ]; then
  rm -rf gcc-13.3.0 b-gcc && tar xf gcc-13.3.0.tar.xz && (cd gcc-13.3.0 && ./contrib/download_prerequisites)
  mkdir b-gcc && cd b-gcc
  ../gcc-13.3.0/configure --target=$T --prefix="$PREFIX" --enable-languages=c --without-headers --with-newlib \
    --disable-nls --disable-shared --disable-threads --disable-libssp --disable-libquadmath --disable-libgomp \
    --disable-libatomic --disable-multilib --disable-bootstrap
  make -j"$J" all-gcc && make install-gcc
  make -j"$J" all-target-libgcc && make install-target-libgcc
  cd "$W"
fi
"$PREFIX/bin/$T-gcc" --version | head -1
echo "Done. Add $PREFIX/bin to your PATH, then run tools/kennel/pack.py."
