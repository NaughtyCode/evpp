#!/bin/bash
# Android NDK build environment configuration for evpp
# This script sets up all required environment variables

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

# --- NDK Configuration ---
if [ -z "$ANDROID_NDK_HOME" ]; then
    # Try to auto-detect NDK location
    if [ -d "/e/Android/ndk/android-ndk-r27c" ]; then
        export ANDROID_NDK_HOME="/e/Android/ndk/android-ndk-r27c"
    elif [ -d "$ROOT_DIR/../Android/ndk/android-ndk-r27c" ]; then
        export ANDROID_NDK_HOME="$ROOT_DIR/../Android/ndk/android-ndk-r27c"
    elif [ -d "/e/Android/ndk/27.2.12479018" ]; then
        export ANDROID_NDK_HOME="/e/Android/ndk/27.2.12479018"
    fi
fi

if [ -z "$ANDROID_NDK_HOME" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "ERROR: ANDROID_NDK_HOME is not set or NDK not found at $ANDROID_NDK_HOME"
    echo "Please set ANDROID_NDK_HOME to your NDK installation path."
    exit 1
fi

echo "ANDROID_NDK_HOME = $ANDROID_NDK_HOME"

# --- Build Configuration ---
export ANDROID_API_LEVEL=21
export ANDROID_ABIS="arm64-v8a armeabi-v7a x86_64 x86"

# Install prefix for built dependencies and evpp
export ANDROID_INSTALL_PREFIX="$ROOT_DIR/android/install"

# Android toolchain
export TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "ERROR: Android CMake toolchain not found at $TOOLCHAIN_FILE"
    echo "NDK may be corrupted or not fully extracted."
    exit 1
fi

echo "TOOLCHAIN_FILE = $TOOLCHAIN_FILE"
echo "ANDROID_API_LEVEL = $ANDROID_API_LEVEL"
echo "ANDROID_ABIS = $ANDROID_ABIS"
echo "ANDROID_INSTALL_PREFIX = $ANDROID_INSTALL_PREFIX"

# Re-export so child scripts inherit
export ROOT_DIR
export SCRIPT_DIR
