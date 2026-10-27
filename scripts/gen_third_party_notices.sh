#!/usr/bin/env bash
# Assemble THIRD-PARTY-NOTICES.txt from the licence files vendored in the SDK
# tree, so the notices that ship in a bundle are the real texts rather than a
# hand-maintained summary that drifts at every SDK bump.
#
# Why this file exists at all: the bundles ship rexruntime.dll (and the mac
# equivalents), which statically links FFmpeg and libmspack -- both LGPL-2.1 --
# plus a stack of permissive components whose licences all require the notice to
# travel with the binary. Before this, the only legal text in a bundle was two
# lines of RUN.txt.
#
# Usage: scripts/gen_third_party_notices.sh [> THIRD-PARTY-NOTICES.txt]
#        SDK=<path> overrides the SDK tree (default: the vendored copy).
set -euo pipefail
cd "$(dirname "$0")/.."

SDK="${SDK:-thirdparty/rexglue-sdk}"
[ -f "$SDK/LICENSE" ] || { echo "ERROR: no SDK licence at $SDK/LICENSE (set SDK=<path>)" >&2; exit 1; }

# component | licence | upstream | path to licence text (relative to $SDK,
# falling back to repo-relative for components that ship from this repo's dist/)
COMPONENTS=(
  "ReXGlue SDK (rexruntime, GPU plugin; incl. Xenia-derived portions)|BSD-3-Clause|https://github.com/rexglue/rexglue-sdk|LICENSE"
  "FFmpeg (libavcodec, libavutil -- LGPL build, CONFIG_GPL=0)|LGPL-2.1-or-later|https://ffmpeg.org|thirdparty/FFmpeg/COPYING.LGPLv2.1"
  "libmspack|LGPL-2.1|https://www.cabextract.org.uk/libmspack/|thirdparty/libmspack/libmspack/COPYING.LIB"
  "SDL3|Zlib|https://www.libsdl.org|thirdparty/sdl3/LICENSE.txt"
  "Dear ImGui|MIT|https://github.com/ocornut/imgui|thirdparty/imgui/LICENSE.txt"
  "Tracy Profiler (TracyClient)|BSD-3-Clause|https://github.com/wolfpld/tracy|thirdparty/tracy/LICENSE"
  "fmt|MIT|https://github.com/fmtlib/fmt|thirdparty/fmt/LICENSE"
  "spdlog|MIT|https://github.com/gabime/spdlog|thirdparty/spdlog/LICENSE"
  "toml++|MIT|https://github.com/marzer/tomlplusplus|thirdparty/tomlplusplus/LICENSE"
  "CLI11|BSD-3-Clause|https://github.com/CLIUtils/CLI11|thirdparty/cli11/LICENSE"
  "inja|MIT|https://github.com/pantor/inja|thirdparty/inja/LICENSE"
  "utfcpp|BSL-1.0|https://github.com/nemtrif/utfcpp|thirdparty/utfcpp/LICENSE"
  "o1heap|MIT|https://github.com/pavel-kirienko/o1heap|thirdparty/o1heap/LICENSE"
  "aes_128|MIT|https://github.com/kokke/tiny-AES-c|thirdparty/aes_128/LICENSE"
  "snappy|BSD-3-Clause|https://github.com/google/snappy|thirdparty/snappy/COPYING"
  "xxHash|BSD-2-Clause|https://github.com/Cyan4973/xxHash|thirdparty/xxHash/cli/COPYING"
  "simde|MIT|https://github.com/simd-everywhere/simde|thirdparty/simde/COPYING"
  "SPIRV-Headers|MIT/Khronos|https://github.com/KhronosGroup/SPIRV-Headers|thirdparty/spirv-headers/LICENSE"
  "SPIRV-Tools|Apache-2.0|https://github.com/KhronosGroup/SPIRV-Tools|thirdparty/spirv-tools/LICENSE"
  "glslang|multiple (see text)|https://github.com/KhronosGroup/glslang|thirdparty/glslang/LICENSE.txt"
  "Vulkan-Headers|Apache-2.0/MIT|https://github.com/KhronosGroup/Vulkan-Headers|thirdparty/vulkan-headers/LICENSE.md"
  "Vulkan Memory Allocator|MIT|https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator|thirdparty/vulkan-memory-allocator/LICENSE.txt"
  "volk|MIT|https://github.com/zeux/volk|thirdparty/volk/LICENSE.md"
  "DES implementation|see text|(vendored in the SDK)|thirdparty/crypto/des/LICENSE"
  "SDL_GameControllerDB (gamecontrollerdb.txt data file)|Zlib|https://github.com/mdqinc/SDL_GameControllerDB|dist/gamecontrollerdb.LICENSE"
)

# Components linked by the SDK that vendor no licence file. Named rather than
# guessed at: quoting a licence text this tree does not contain would be worse
# than admitting the gap.
UNRESOLVED=(
  "picosha2 (MIT upstream)"
  "stb (MIT / public domain upstream)"

  "DirectXShaderCompiler / dxc (Apache-2.0 with LLVM exceptions upstream)"
  "disruptorplus, o1heap-adjacent helpers, disasm, dxbc, pe, ffmpeg-overlay (SDK-local)"
)

emit() {
  echo "THIRD-PARTY NOTICES -- Mushihimesama Futari (recompiled)"
  echo "======================================================="
  echo
  echo "This build of the port links the third-party components listed below. Their"
  echo "licences are reproduced in full, as those licences require when the software"
  echo "is distributed in binary form."
  echo
  echo "This file covers third-party code ONLY. It says nothing about the game:"
  echo "Mushihimesama Futari and all of its code, data, audio and artwork belong to"
  echo "Cave Interactive Co., Ltd. See NOTICE.md and LICENSE in the source repository"
  echo "for the port's own terms and their limits."
  echo
  echo "LGPL-2.1 NOTICE AND WRITTEN OFFER"
  echo "---------------------------------"
  echo "FFmpeg (libavcodec, libavutil) and libmspack are licensed under the LGPL,"
  echo "version 2.1, and are linked statically into the ReXGlue runtime shipped here."
  echo "You are entitled to the complete corresponding source for those libraries, and"
  echo "to the object code needed to relink the runtime against a modified version of"
  echo "them. Both are obtainable from this port's source repository:"
  echo
  echo "    https://github.com/springah/futari_recomp"
  echo
  echo "The exact SDK this runtime was built from is vendored there under"
  echo "thirdparty/rexglue-sdk at the release tag matching this build, with FFmpeg and"
  echo "libmspack as its thirdparty/FFmpeg and thirdparty/libmspack submodules and the"
  echo "build files used to produce the runtime. If you cannot obtain them there, open"
  echo "an issue in that repository and they will be provided."
  echo
  echo "COMPONENTS"
  echo "----------"
  for entry in "${COMPONENTS[@]}"; do
    IFS='|' read -r name lic url _path <<<"$entry"
    printf '  %-58s %-22s %s\n' "$name" "$lic" "$url"
  done
  echo
  echo "  Linked components whose licence text is not vendored in the SDK tree, and"
  echo "  which therefore are NOT reproduced below (upstream licence named for"
  echo "  reference only -- this gap should be closed in the SDK):"
  for u in "${UNRESOLVED[@]}"; do echo "    - $u"; done
  echo
  echo "  On macOS the bundle also ships MoltenVK (Apache-2.0). Its licence is copied"
  echo "  into the bundle beside this file by mac-build/build_app.sh."
  echo

  local missing=0
  for entry in "${COMPONENTS[@]}"; do
    IFS='|' read -r name lic url path <<<"$entry"
    echo
    echo "==============================================================================="
    echo "$name"
    echo "$lic -- $url"
    echo "==============================================================================="
    echo
    if [ -f "$SDK/$path" ]; then
      cat "$SDK/$path"
    elif [ -f "$path" ]; then
      cat "$path"
    else
      echo "  [licence text not found at $path in the SDK tree -- see $url]"
      missing=$((missing + 1))
    fi
    echo
  done
  if [ "$missing" -gt 0 ]; then
    echo "ERROR: $missing licence text(s) were missing from the SDK tree; the output is incomplete." >&2
    return 1
  fi
}

emit
