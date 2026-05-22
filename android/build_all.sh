#!/bin/bash
# Master build script for evpp Android NDK cross-compilation
# Builds dependencies + evpp for all target ABIs
#
# Usage:
#   ./build_all.sh              # Build everything for all ABIs
#   ./build_all.sh arm64-v8a    # Build for specific ABI only
#
# Environment variables:
#   ANDROID_NDK_HOME   - Path to Android NDK (auto-detected if not set)
#   NJOBS              - Number of parallel jobs (default: auto)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo ""
echo "=============================================="
echo " evpp Android NDK Build"
echo "=============================================="
echo ""

# Step 1: Environment check
echo ">>> Step 1/3: Checking environment..."
source "$SCRIPT_DIR/build_env.sh"
echo "OK."

# Step 2: Build dependencies (libevent, glog)
echo ""
echo ">>> Step 2/3: Building dependencies..."
bash "$SCRIPT_DIR/build_deps.sh" "$@"
echo "Dependencies built."

# Step 3: Build evpp
echo ""
echo ">>> Step 3/3: Building evpp..."
bash "$SCRIPT_DIR/build_evpp.sh" "$@"

echo ""
echo "=============================================="
echo " BUILD SUCCESSFUL"
echo "=============================================="
echo ""
echo "Output layout ($ANDROID_INSTALL_PREFIX):"
find "$ANDROID_INSTALL_PREFIX" -type f -name "*.a" 2>/dev/null || echo "(no .a files found)"
echo ""
echo "Usage in your Android project's CMakeLists.txt:"
echo ""
echo "  set(EVPP_DIR \"\${CMAKE_CURRENT_SOURCE_DIR}/path/to/android/install/evpp/\${ANDROID_ABI}\")"
echo "  target_include_directories(your_target PRIVATE \${EVPP_DIR}/include)"
echo "  target_link_libraries(your_target \${EVPP_DIR}/lib/libevpp_static.a)"
echo "  target_link_libraries(your_target log android)"
echo ""
