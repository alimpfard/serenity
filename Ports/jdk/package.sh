#!/usr/bin/env -S bash ../.port_include.sh
port=jdk
version=git
workdir=jdk-master
useconfigure=true
makeopts="JOBS=$(nproc)"
depends="freetype"
files="https://github.com/openjdk/jdk/archive/refs/heads/master.tar.gz jdk.tar.gz"

configure() {
    chmod +x "${workdir}"/"$configscript"
    run ./"$configscript" --openjdk-target=i686-pc-serenity \
        --with-sysroot=$(realpath $DESTDIR) \
        --with-toolchain-path=$(realpath ../../Toolchain/Local/i686/bin) \
        --prefix=$(realpath $DESTDIR/usr/local) \
        --with-extra-cflags="-Wno-error=format -Wno-error=deprecated-declarations -DMEAN_PORTS_STEALING_GOOD_NAMES"
}
