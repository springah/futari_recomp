#!/usr/bin/env bash
# Stages the public Windows zip (asset-free) from a release build and checks it against the shipping rules.
#
#   bash scripts/stage_win_zip.sh 1.5.5                       # out/build/win-amd64-release-shaders -> out/Mushihimesama-Futari-PC-v1.5.5.zip
#   BUILD_DIR=out/build/other bash scripts/stage_win_zip.sh 1.5.5-rc1
#
# The build must be the RELEASE SHADER build (-DFUTARI_RENUT_RELEASE_SHADERS=ON): its futari.exe carries the Renut
# shader table but no game shader. The player's extractor translates their own shaders into vulkan_spirv.bin with the
# translator built into futari.exe (XenosRecomp) and the files in renut_tools/ (the DirectX Shader Compiler DLL, the
# shader header, the early-alpha pass; no executable), driven by make_shaders.py. A build with the shaders built in
# (the dev tree) is game data and is refused here.
#
# Layout (single root, as every public zip since v1.5.3; + renut_tools/ and make_shaders.py from this version on):
#   Mushihimesama-Futari-PC/
#     futari.exe futari.ini futari_<core>.dll x8 rexgpu-xenos.dll rexruntime.dll
#     extract_assets.bat extract_iso.py make_shaders.py renut_tools/{dxcompiler.dll,shader_common.h,
#     renut_early_alpha.py,early_alpha.py}
#     RUN.txt gamecontrollerdb.txt THIRD-PARTY-NOTICES.txt dlc/README.txt
# Not shipped: assets/, dlc/<ids>/, vulkan_spirv.bin + vulkan_spirv.stamp, shader_work/, logs/, TracyClient.dll (not imported).
set -euo pipefail
VER="${1:?usage: stage_win_zip.sh <version, e.g. 1.5.5>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/out/build/win-amd64-release-shaders}"
NAME="Mushihimesama-Futari-PC"
STAGE="$ROOT/out/_stage/$NAME"
ZIP="$ROOT/out/$NAME-v$VER.zip"

[ -f "$BUILD_DIR/futari.exe" ] || { echo "no $BUILD_DIR/futari.exe"; exit 1; }
grep -q 'FUTARI_RENUT_RELEASE_SHADERS:BOOL=ON' "$BUILD_DIR/CMakeCache.txt" ||
  { echo "REFUSED: $BUILD_DIR was not configured with FUTARI_RENUT_RELEASE_SHADERS=ON (its exe would carry game shaders)"; exit 1; }
for t in dxcompiler.dll shader_common.h renut_early_alpha.py early_alpha.py; do
  [ -f "$BUILD_DIR/renut_tools/$t" ] || { echo "no $BUILD_DIR/renut_tools/$t: build the futari_renut_tools target"; exit 1; }
done

rm -rf "$ROOT/out/_stage"
mkdir -p "$STAGE/dlc" "$STAGE/renut_tools"
cp "$BUILD_DIR"/futari.exe "$BUILD_DIR"/futari_*.dll "$BUILD_DIR"/rexgpu-xenos.dll "$BUILD_DIR"/rexruntime.dll "$STAGE/"
cp "$BUILD_DIR"/renut_tools/{dxcompiler.dll,shader_common.h,renut_early_alpha.py,early_alpha.py} "$STAGE/renut_tools/"
# No executable ships in renut_tools/ (2026-11-03): the translator is built into futari.exe; Windows Defender quarantined
# v1.5.5-rc1's standalone XenosRecomp.exe.
ls "$STAGE"/renut_tools/*.exe >/dev/null 2>&1 && { echo "REFUSED: an executable in renut_tools/"; exit 1; }
cp "$ROOT/extract_assets.bat" "$ROOT/scripts/extract_iso.py" "$ROOT/scripts/make_shaders.py" "$STAGE/"
cp "$ROOT/dist/RUN.txt" "$ROOT/dist/gamecontrollerdb.txt" "$ROOT/THIRD-PARTY-NOTICES.txt" "$STAGE/"
cp "$ROOT/dist/dlc-README.txt" "$STAGE/dlc/README.txt"
# futari.ini: the exe writes its defaults on first run; the build tree's copy is that file.
cp "$BUILD_DIR/futari.ini" "$STAGE/"

cores=$(ls "$STAGE"/futari_*.dll | wc -l)
[ "$cores" -eq 8 ] || { echo "REFUSED: $cores core DLLs, need 8 (build every target)"; exit 1; }

# Shipping rules: nothing of the game, nothing translated from it, nothing from a run.
bad=$(cd "$STAGE" && find . \( -name '*.xex' -o -name 'vulkan_spirv.bin' -o -name 'vulkan_spirv.stamp' -o -name '*.bin' -o -path './assets*' -o -path './logs*' \
      -o -path './shader_work*' -o -path './shader_capture*' -o -name '*.log' \) | head)
if [ -n "$bad" ]; then echo "REFUSED: game data or run output in the stage:"; echo "$bad"; exit 1; fi
# The exe must not carry the game's shaders: started with Renut and no package it must say so (the dev build would
# draw). This also proves the staged DLL set boots. ASSETS = a game data folder (never copied into the stage).
ASSETS="${ASSETS:-$ROOT/assets}"
[ -f "$ASSETS/default.xex" ] || { echo "no $ASSETS/default.xex (set ASSETS=) for the boot check"; exit 1; }
( cd "$STAGE" && timeout 12 ./futari.exe --game_data_root="$(cygpath -m "$ASSETS" 2>/dev/null || echo "$ASSETS")" --futari_renderer=renut >/dev/null 2>&1 || true )
taskkill //F //IM futari.exe >/dev/null 2>&1 || true
LOG=$(ls -t "$STAGE"/logs/*.log 2>/dev/null | head -1)
grep -q "no game shaders built in" "${LOG:-/dev/null}" ||
  { echo "REFUSED: the staged exe did not report an empty shader table (dev build with the shaders built in, or it did not boot); see $LOG"; exit 1; }
grep -q "Renderer: Vulkan" "$LOG" || { echo "REFUSED: the staged exe did not bring Renut up; see $LOG"; exit 1; }
rm -rf "$STAGE/logs" "$STAGE/shader_capture"
echo "  boot check: release shader table, Renut up"
# The exe must carry the translator (the tool mode make_shaders.py drives): asked to translate a folder that does not
# exist it must refuse with exit 2 within seconds. An exe without the mode ignores the unknown cvar, boots the game
# and times out, and make_shaders.py would do the same on the player's PC.
translate_rc=0
( cd "$STAGE" && timeout 15 ./futari.exe --game_data_root="$(cygpath -m "$ASSETS" 2>/dev/null || echo "$ASSETS")" --futari_renut_translate="$(cygpath -m "$STAGE")/no_such_dump" >/dev/null 2>&1 ) || translate_rc=$?
taskkill //F //IM futari.exe >/dev/null 2>&1 || true
rm -rf "$STAGE/logs"
[ "$translate_rc" = "2" ] || { echo "REFUSED: the staged exe has no shader translator tool mode (exit $translate_rc, expected 2)"; exit 1; }
echo "  tool check: the translator is built in"
# The default renderer must survive its first dialogs (the title hint appears a few seconds in): a crash there leaves a
# log identical to a healthy run's, so judge by the exit: 124 = still running when timeout killed it.
xenos_rc=0
( cd "$STAGE" && timeout 20 ./futari.exe --game_data_root="$(cygpath -m "$ASSETS" 2>/dev/null || echo "$ASSETS")" >/dev/null 2>&1 ) || xenos_rc=$?
taskkill //F //IM futari.exe >/dev/null 2>&1 || true
[ "$xenos_rc" = "124" ] || { echo "REFUSED: the staged exe died within 20 s on the default renderer (exit $xenos_rc)"; exit 1; }
rm -rf "$STAGE/logs"
echo "  boot check: default renderer alive at 20 s"

rm -f "$ZIP"
python3 - "$ROOT/out/_stage" "$NAME" "$ZIP" <<'EOF'
import os, sys, zipfile, hashlib
root, name, out = sys.argv[1:]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for dp, dn, fn in os.walk(os.path.join(root, name)):
        dn.sort()
        for f in sorted(fn):
            p = os.path.join(dp, f)
            z.write(p, os.path.relpath(p, root).replace(os.sep, '/'))
h = hashlib.sha256(open(out, 'rb').read()).hexdigest()
open(out + '.sha256', 'w').write(f"{h}  {os.path.basename(out)}\n")
print(f"  {out}: {os.path.getsize(out) // 1024 // 1024} MiB, sha256 {h}")
EOF
echo "[DONE] stage: $STAGE"
