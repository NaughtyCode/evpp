#!/bin/bash
# Build dependencies (libevent, glog) for Android NDK
# Creates a merged sysroot for each ABI that evpp can consume
# Usage: ./build_deps.sh [abi1 abi2 ...]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_env.sh"

DEPS_DIR="$ROOT_DIR/android/deps_src"
SYSROOT_DIR="$ROOT_DIR/android/sysroot"
mkdir -p "$DEPS_DIR" "$SYSROOT_DIR"

# --- Config ---
LIBEVENT_VERSION="2.1.12-stable"
LIBEVENT_URL="https://github.com/libevent/libevent/releases/download/release-${LIBEVENT_VERSION}/libevent-${LIBEVENT_VERSION}.tar.gz"

GLOG_VERSION="0.6.0"
GLOG_URL="https://github.com/google/glog/archive/refs/tags/v${GLOG_VERSION}.tar.gz"

NJOBS=${NJOBS:-$(nproc 2>/dev/null || echo 4)}

# --- CMake flags common to all deps ---
common_cmake_args() {
    local abi="$1"
    echo "-G" "Ninja"
    echo "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"
    echo "-DANDROID_ABI=$abi"
    echo "-DANDROID_PLATFORM=android-$ANDROID_API_LEVEL"
    echo "-DANDROID_STL=c++_static"
    echo "-DCMAKE_BUILD_TYPE=Release"
    echo "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    echo "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    # Android has pthread built into bionic libc, not a separate library
    echo "-DTHREADS_PTHREAD_ARG=-pthread"
    # Static library check for cross-compilation (avoids linking executables)
    echo "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
}

download() {
    local url="$1"
    local dest="$2"
    if [ ! -f "$dest" ]; then
        echo "Downloading: $url"
        curl -L --progress-bar -o "$dest" "$url"
    else
        echo "Already downloaded: $dest"
    fi
}

build_libevent() {
    local abi="$1"
    local src_dir="$DEPS_DIR/libevent-${LIBEVENT_VERSION}"
    local build_dir="$DEPS_DIR/build_libevent_${abi}"

    echo ""
    echo "===================================================================="
    echo "Building libevent ${LIBEVENT_VERSION} for ${abi}"
    echo "===================================================================="

    if [ ! -d "$src_dir" ]; then
        download "$LIBEVENT_URL" "$DEPS_DIR/libevent.tar.gz"
        echo "Extracting libevent..."
        tar xzf "$DEPS_DIR/libevent.tar.gz" -C "$DEPS_DIR"
    fi

    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    cd "$build_dir"

    cmake "$src_dir" \
        $(common_cmake_args "$abi") \
        -DEVENT__DISABLE_TESTS=ON \
        -DEVENT__DISABLE_SAMPLES=ON \
        -DEVENT__DISABLE_BENCHMARK=ON \
        -DEVENT__DISABLE_OPENSSL=ON \
        -DEVENT__DISABLE_MBEDTLS=ON \
        -DEVENT__LIBRARY_TYPE=STATIC

    ninja -j"$NJOBS"

    # Install to merged sysroot for this ABI
    local abi_sysroot="$SYSROOT_DIR/$abi"
    mkdir -p "$abi_sysroot/lib" "$abi_sysroot/include"
    cp -v "$build_dir/lib/"*.a "$abi_sysroot/lib/" 2>/dev/null || true
    cp -rv "$build_dir/include/"* "$abi_sysroot/include/" 2>/dev/null || true
    cp -rv "$src_dir/include/"* "$abi_sysroot/include/" 2>/dev/null || true

    echo "libevent for ${abi} done."
}

build_glog() {
    local abi="$1"
    local src_dir="$DEPS_DIR/glog-${GLOG_VERSION}"
    local build_dir="$DEPS_DIR/build_glog_${abi}"

    echo ""
    echo "===================================================================="
    echo "Building glog ${GLOG_VERSION} for ${abi}"
    echo "===================================================================="

    if [ ! -d "$src_dir" ]; then
        download "$GLOG_URL" "$DEPS_DIR/glog.tar.gz"
        echo "Extracting glog..."
        tar xzf "$DEPS_DIR/glog.tar.gz" -C "$DEPS_DIR"
    fi

    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    cd "$build_dir"

    cmake "$src_dir" \
        $(common_cmake_args "$abi") \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DWITH_GFLAGS=OFF \
        -DWITH_GTEST=OFF \
        -DWITH_UNWIND=OFF

    ninja -j"$NJOBS"

    # Install to merged sysroot for this ABI
    local abi_sysroot="$SYSROOT_DIR/$abi"
    mkdir -p "$abi_sysroot/lib" "$abi_sysroot/include"
    cp -v "$build_dir/"*.a "$abi_sysroot/lib/" 2>/dev/null || true
    cp -rv "$src_dir/src/glog" "$abi_sysroot/include/" 2>/dev/null || true
    if [ -d "$build_dir/glog" ]; then
        cp -rv "$build_dir/glog/"*.h "$abi_sysroot/include/glog/" 2>/dev/null || true
    fi

    echo "glog for ${abi} done."
}

# --- Main ---

ABI_LIST="$@"
if [ -z "$ABI_LIST" ]; then
    ABI_LIST="$ANDROID_ABIS"
fi

echo ""
echo "===================================================================="
echo " Building evpp dependencies for Android"
echo " ABIs: $ABI_LIST"
echo "===================================================================="

for abi in $ABI_LIST; do
    build_libevent "$abi"
    build_glog "$abi"

    echo ""
    echo ">>> Sysroot for $abi:"
    echo "    $SYSROOT_DIR/$abi/"
    find "$SYSROOT_DIR/$abi" -type f 2>/dev/null | sort || echo "    (empty)"
done

echo ""
echo "===================================================================="
echo " All dependencies built successfully!"
echo " Sysroot: $SYSROOT_DIR/"
echo "===================================================================="
