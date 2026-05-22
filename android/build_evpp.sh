#!/bin/bash
# Build evpp library for Android NDK
# Uses the merged sysroot created by build_deps.sh
# Usage: ./build_evpp.sh [abi1 abi2 ...]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_env.sh"

SYSROOT_DIR="$ROOT_DIR/android/sysroot"
NJOBS=${NJOBS:-$(nproc 2>/dev/null || echo 4)}

build_evpp_for_abi() {
    local abi="$1"
    local build_dir="$ROOT_DIR/android/build_evpp_${abi}"
    local abi_sysroot="$SYSROOT_DIR/$abi"
    local install_dir="$ANDROID_INSTALL_PREFIX/evpp/${abi}"

    echo ""
    echo "===================================================================="
    echo " Building evpp for ${abi}"
    echo "===================================================================="

    if [ ! -d "$abi_sysroot/lib" ]; then
        echo "ERROR: Dependency sysroot not found at $abi_sysroot"
        echo "Run build_deps.sh first."
        exit 1
    fi

    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    cd "$build_dir"

    # Copy dependency headers to 3rdparty so they're in evpp's include path
    # (evpp CMakeLists.txt replaces CMAKE_CXX_FLAGS, so -I flags are lost)
    rm -rf "$ROOT_DIR/3rdparty/glog" "$ROOT_DIR/3rdparty/event2" 2>/dev/null || true
    cp -r "$abi_sysroot/include/glog" "$ROOT_DIR/3rdparty/" 2>/dev/null || true
    cp -r "$abi_sysroot/include/event2" "$ROOT_DIR/3rdparty/" 2>/dev/null || true
    for h in event.h evdns.h evhttp.h evrpc.h evutil.h evconfig-private.h; do
        cp "$abi_sysroot/include/$h" "$ROOT_DIR/3rdparty/" 2>/dev/null || true
    done

    echo "Using sysroot: $abi_sysroot"
    echo "Sysroot contents:"
    find "$abi_sysroot" -type f -name "*.a" -o -name "*.h" 2>/dev/null | head -20

    # Build evpp with CMake
    # Key: CMAKE_FIND_ROOT_PATH tells CMake where to find cross-compiled deps
    cmake "$ROOT_DIR" \
        -G "Ninja" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM="android-${ANDROID_API_LEVEL}" \
        -DANDROID_STL="c++_static" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$install_dir" \
        -DEVPP_VCPKG_BUILD=ON \
        -DCMAKE_CXX_STANDARD=11 \
        -DCMAKE_FIND_ROOT_PATH="$abi_sysroot" \
        -DCMAKE_SYSROOT="$abi_sysroot" \
        -DCMAKE_C_FLAGS="-I$abi_sysroot/include" \
        -DCMAKE_CXX_FLAGS="-I$abi_sysroot/include" \
        -DCMAKE_LIBRARY_PATH="$abi_sysroot/lib" \
        -DCMAKE_MODULE_PATH="$ROOT_DIR/cmake" \
        -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY

    # Build only static library targets
    ninja -j"$NJOBS" evpp_static evpp_lite_static

    # Collect output
    mkdir -p "$install_dir/lib" "$install_dir/include"
    cp -v "$build_dir/lib/"*.a "$install_dir/lib/" 2>/dev/null || true

    # Copy public headers
    if [ -d "$ROOT_DIR/evpp" ]; then
        for subdir in "" http httpc udp evpphttp; do
            if [ -d "$ROOT_DIR/evpp/$subdir" ]; then
                mkdir -p "$install_dir/include/evpp/$subdir"
                cp -rv "$ROOT_DIR/evpp/$subdir/"*.h "$install_dir/include/evpp/$subdir/" 2>/dev/null || true
            fi
        done
    fi

    echo ""
    echo "evpp for ${abi} completed."
    ls -la "$install_dir/lib/" 2>/dev/null
}

# --- Main ---

ABI_LIST="$@"
if [ -z "$ABI_LIST" ]; then
    ABI_LIST="$ANDROID_ABIS"
fi

echo ""
echo "===================================================================="
echo " Building evpp for Android"
echo " ABIs: $ABI_LIST"
echo "===================================================================="

for abi in $ABI_LIST; do
    build_evpp_for_abi "$abi"
done

echo ""
echo "===================================================================="
echo " evpp build complete for all ABIs!"
echo ""
echo " Output: $ANDROID_INSTALL_PREFIX/evpp/"
echo "===================================================================="
find "$ANDROID_INSTALL_PREFIX/evpp/" -type f 2>/dev/null | sort || echo "(empty)"
