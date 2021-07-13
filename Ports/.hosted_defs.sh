#!/usr/bin/env bash
export SERENITY_SOURCE_DIR="$(realpath "${SCRIPT}/../")"

: ${SERENITY_USE_CLANG_TOOLCHAIN:=}

if [ -n "$SERENITY_USE_CLANG_TOOLCHAIN" ]; then
    export PATH="${SERENITY_SOURCE_DIR}/Toolchain/Local/clang/${SERENITY_ARCH}/bin:${HOST_PATH}"
    export SERENITY_BUILD_DIR="${SERENITY_SOURCE_DIR}/Build/Clang"
    export SERENITY_SYSROOT="${SERENITY_BUILD_DIR}/Root"
    export CC="clang --sysroot=$SERENITY_SYSROOT"
    export CXX="clang++ --sysroot=$SERENITY_SYSROOT"
    export CPP="clang-cpp --sysroot=$SERENITY_SYSROOT"
    export CXXCPP="clang-cpp --sysroot=$SERENITY_SYSROOT"
    export AR="llvm-ar"
    export RANLIB="llvm-ranlib"
    export READELF="llvm-readelf"
    export CFLAGS="-I${SERENITY_BUILD_DIR}/Root/usr/include -L${SERENITY_BUILD_DIR}/Root/usr/lib --sysroot=$SERENITY_SYSROOT -ftls-model=initial-exec -fPIC"
else
    export PATH="${SERENITY_SOURCE_DIR}/Toolchain/Local/${SERENITY_ARCH}/bin:${HOST_PATH}"
    export SERENITY_BUILD_DIR="${SERENITY_SOURCE_DIR}/Build/$SERENITY_ARCH"
    export SERENITY_SYSROOT="${SERENITY_BUILD_DIR}/Root"
    export CC="${SERENITY_ARCH}-pc-serenity-gcc"
    export CXX="${SERENITY_ARCH}-pc-serenity-g++"
    export CPP="${SERENITY_ARCH}-pc-serenity-cpp"
    export CXXCPP="${SERENITY_ARCH}-pc-serenity-cpp"
    export AR="${SERENITY_ARCH}-pc-serenity-ar"
    export RANLIB="${SERENITY_ARCH}-pc-serenity-ranlib"
fi

export PKG_CONFIG_DIR=""
export PKG_CONFIG_SYSROOT_DIR="${SERENITY_BUILD_DIR}/Root"
export PKG_CONFIG_LIBDIR="${PKG_CONFIG_SYSROOT_DIR}/usr/lib/pkgconfig/:${PKG_CONFIG_SYSROOT_DIR}/usr/local/lib/pkgconfig"

# To be deprecated soon.
export SERENITY_ROOT="$(realpath "${SCRIPT}/../")"

enable_ccache

DESTDIR="${SERENITY_BUILD_DIR}/Root"
export SERENITY_INSTALL_ROOT="$DESTDIR"
