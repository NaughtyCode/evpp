#!/bin/bash
# Quick setup: Set ANDROID_NDK_HOME environment variable
# Source this file to configure your shell:
#   source android/setup_ndk.sh

NDK_PATH=""
for candidate in \
    "/e/Android/ndk/android-ndk-r27c" \
    "/e/Android/ndk/27.2.12479018" \
    "$HOME/Android/Sdk/ndk/27.2.12479018" \
    "$HOME/Android/ndk/android-ndk-r27c"; do
    if [ -d "$candidate" ]; then
        NDK_PATH="$candidate"
        break
    fi
done

if [ -z "$NDK_PATH" ]; then
    echo "ERROR: NDK not found. Please install it first."
    return 1 2>/dev/null || exit 1
fi

export ANDROID_NDK_HOME="$NDK_PATH"
export PATH="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/windows-x86_64/bin:$PATH"
echo "ANDROID_NDK_HOME = $ANDROID_NDK_HOME"
echo "Added NDK tools to PATH"
