#!/usr/bin/env bash
# Build futari for macOS using the KNOWN-GOOD Windows recompiled code bodies on
# top of our mac-compatible codegen glue.
#
# WHY: our rexglue (0.8.1.93, from the PR#356 merge) emits some function bodies
# differently than the rexglue (0.8.1.28) that built the stable Windows DLLs —
# e.g. an internal `goto loc_880EC954` became a call into a gap, trapping at
# reco character-select. The Windows generated/ has the correct bodies but its
# glue uses __attribute__((alias(...))) which Mach-O can't link. So:
#   - GLUE (futari_init.h/.cpp, futari_register.cpp) = OURS (mac-compatible)
#   - CODE BODIES (futari_recomp.*.cpp)              = WINDOWS (correct)
#   - dll_targets.cmake / module_registry.cpp        = all-8 assembly (Windows copy)
#   - generated/rexglue.cmake                         = OURS (refs generated/default)
#
# Prereq: a clean `bash scripts/regen.sh` has produced generated/ with OUR glue
# for all modules (no manual function-entry seeds). Then run this to overlay.
set -euo pipefail
REPO="$HOME/Desktop/projects/futari-mac/futari_recomp"
WIN="$HOME/Desktop/projects/futari_windows_generated"
cd "$REPO"

MODS=(default reco_normal reco_abnormal palm_normal palm_abnormal hirow_normal hirow_abnormal kiniro_normal kiniro_abnormal)
for d in "${MODS[@]}"; do
  cp "$WIN/$d"/futari_recomp.*.cpp "generated/$d"/
  echo "overlaid bodies: $d"
done
# all-8 build glue (regen.sh's solo path can clobber these to single-module)
cp "$WIN/default/dll_targets.cmake"   generated/default/dll_targets.cmake
cp "$WIN/default/module_registry.cpp" generated/default/module_registry.cpp

export PATH="/opt/homebrew/bin:$PATH"; export CMAKE_PREFIX_PATH="/opt/homebrew/opt/llvm"
cmake --preset mac-arm64-release -DREXSDK_DIR="$REPO/thirdparty/rexglue-sdk" -DREXGLUE_BUILD_TESTS=OFF
cmake --build out/build/mac-arm64-release --target futari \
  futari_reco_normal futari_reco_abnormal futari_palm_normal futari_palm_abnormal \
  futari_hirow_normal futari_hirow_abnormal futari_kiniro_normal futari_kiniro_abnormal
echo "DONE — stage out/build/mac-arm64-release/{futari,libfutari_*.dylib} + librexruntime into Futari.app, then re-codesign."
