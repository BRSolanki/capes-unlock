#!/usr/bin/env bash
# build_android.sh — Build libcapes.so for Android
# Usage: ./build_android.sh [abi]
# Requires: ANDROID_NDK_HOME env var, Python 3
set -euo pipefail

: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to your NDK path}"
: "${ANDROID_ABI:=${1:-arm64-v8a}}"
: "${ANDROID_PLATFORM:=android-24}"

echo "=== Step 1: Generate C++ headers from PocketCosmos assets ==="
python3 generate_headers.py \
    --cosmos-assets assets/cosmos \
    --legal-assets assets/legal \
    --legal-src assets/legal_src \
    --output-dir generated

echo ""
echo "=== Step 2: Configure CMake for $ANDROID_ABI ==="
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ANDROID_ABI" \
  -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
  -DCMAKE_BUILD_TYPE=Release

echo ""
echo "=== Step 3: Build ==="
cmake --build build --config Release -j"$(nproc)"

echo ""
echo "✅ Output: build/libcapes.so"
file build/libcapes.so
ls -la build/libcapes.so
