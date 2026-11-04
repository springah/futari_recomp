#!/usr/bin/env bash
# Build Microsoft's DirectX Shader Compiler for Apple Silicon and install it where this tree looks for it:
#   thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/bin/arm64/dxc-macos       (Renut's host shaders, at build time)
#   thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/lib/arm64/libdxcompiler.dylib
#     (XenosRecomp's build-time shader cache, and the copy the exe's translator loads with dlopen at the player's
#      setup step: renut_tools/ in the build tree, Contents/lib in the .app)
# Both paths are git-ignored: binaries are never committed.
#
# Why from source: there is no macOS DXC to download from an official place. Homebrew has no formula, hedge-dev's
# dxc-bin (which XenosRecomp vendors) ships Windows binaries only, and the LunarG Vulkan SDK's copy would make a
# 2 GB installer a build dependency. The source build is one-off: ~10 min on 6 cores of an M3 Max, 26 MB dylib that
# links only libSystem, libc++ and libz from the OS. Version v1.8.2407, the release the Windows tree's dxc.exe and
# dxcompiler.dll are (docs/NOTES_renut_leg6_macos.md).
#
# Two patches for today's toolchain, applied to the clone only: CMake 4 refuses cmake_policy(SET CMP0051 OLD) (the
# line is dropped; CMAKE_POLICY_VERSION_MINIMUM=3.5 for the rest) and the Xcode 26 libc++ refuses LLVM 3.7's
# std::is_nothrow_constructible specialisations unless -Wno-invalid-specialization.
#
#   scripts/build_dxc_mac.sh [work dir, default out/dxc]     (JOBS=6 by default)
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"
WORK="${1:-$REPO/out/dxc}"
JOBS="${JOBS:-6}"
VERSION="v1.8.2407"
DXC_BIN="$REPO/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin"

mkdir -p "$WORK"
if [ ! -d "$WORK/src" ]; then
  git clone --depth 1 -b "$VERSION" --recurse-submodules --shallow-submodules \
    https://github.com/microsoft/DirectXShaderCompiler.git "$WORK/src"
fi
sed -i '' 's/^  cmake_policy(SET CMP0051 OLD)/  # cmake_policy(SET CMP0051 OLD)  futari: CMake 4 refuses OLD/' "$WORK/src/CMakeLists.txt"

cmake -S "$WORK/src" -B "$WORK/build" -G Ninja -C "$WORK/src/cmake/caches/PredefinedParams.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DSPIRV_BUILD_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF \
  -DLLVM_ENABLE_WERROR=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  "-DCMAKE_CXX_FLAGS=-Wno-invalid-specialization -Wno-deprecated-declarations -Wno-vla-cxx-extension"
ninja -C "$WORK/build" -j"$JOBS" dxc dxcompiler

mkdir -p "$DXC_BIN/bin/arm64" "$DXC_BIN/lib/arm64"
cp -f "$WORK/build/lib/libdxcompiler.dylib" "$DXC_BIN/lib/arm64/libdxcompiler.dylib"
cp -f "$(readlink -f "$WORK/build/bin/dxc")" "$DXC_BIN/bin/arm64/dxc-macos"
# The binary looks for @rpath/libdxcompiler.dylib in @executable_path/../lib; dxc-bin keeps it in ../../lib/arm64.
install_name_tool -add_rpath @loader_path/../../lib/arm64 "$DXC_BIN/bin/arm64/dxc-macos"
codesign -f -s - "$DXC_BIN/bin/arm64/dxc-macos"
"$DXC_BIN/bin/arm64/dxc-macos" --version
cp -f "$WORK/src/LICENSE.TXT" "$DXC_BIN/lib/arm64/LICENSE-DirectXShaderCompiler.txt"
echo "installed into $DXC_BIN/{bin,lib}/arm64"
