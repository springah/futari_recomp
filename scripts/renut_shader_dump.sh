#!/usr/bin/env bash
# Renut's shader dump: out/renut_shader_dump/{vs,ps}_<xxh3>.bin + manifest.csv, the input of the real shader-cache
# pipeline (cmake/futari_renut_gpu.cmake, FUTARI_RENUT_STUB_SHADER_CACHE=OFF). Two steps:
#
#  1. out/<module>_image.bin: the decrypted, decompressed images of default.xex and the eight game cores, written by
#     the codegen CLI's REXGLUE_DUMP_IMAGE aid (thirdparty/rexglue-sdk/src/codegen/project_recompiler.cpp). One
#     isolated manifest per core, as scripts/regen.sh (the cores share base 0x88000000); the codegen output goes to
#     out/imgdump/ so generated/ is untouched (bar rexglue.cmake, which is saved and restored). A few seconds per module.
#  2. tools/renut/renut_dump_shaders.py: the shader containers in those images (Futari compiles its shaders into its
#     executables: 22 + 22 in default.xex, 2 + 2 per core, 42 unique) -> out/renut_shader_dump.
#
# Needs the rexglue CLI (built with the tree: thirdparty/rexglue-sdk/out/<platform>/rexglue[.exe], or REXGLUE=<path>),
# python3 with the xxhash module (pip install xxhash), and the game data in assets/. The output is game data: out/ is
# ignored, never commit it.
#
#   scripts/renut_shader_dump.sh            # both steps (images reused when present; delete out/*_image.bin to redo)
#   scripts/renut_shader_dump.sh --force    # redo the images too
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -z "${REXGLUE:-}" ]; then
  for c in thirdparty/rexglue-sdk/out/win-amd64/rexglue.exe \
           thirdparty/rexglue-sdk/out/linux-amd64/rexglue \
           thirdparty/rexglue-sdk/out/mac-arm64/rexglue \
           thirdparty/rexglue-sdk/out/mac-arm64/Release/rexglue; do
    if [ -x "$c" ]; then REXGLUE="$c"; break; fi
  done
fi
[ -n "${REXGLUE:-}" ] && [ -x "$REXGLUE" ] || { echo "ERROR: rexglue CLI not found (build the tree first, or REXGLUE=<path>)" >&2; exit 1; }
[ -f assets/default.xex ] || { echo "ERROR: no assets/default.xex (the game data)" >&2; exit 1; }

MODS=(reco_normal reco_abnormal palm_normal palm_abnormal hirow_normal hirow_abnormal kiniro_normal kiniro_abnormal)
force=0; [ "${1:-}" = "--force" ] && force=1
need=0
for m in default "${MODS[@]}"; do [ -f "out/${m}_image.bin" ] || need=1; done

if [ $need = 1 ] || [ $force = 1 ]; then
  mkdir -p out/imgdump
  # The codegen CLI rewrites generated/rexglue.cmake on every run, whatever the manifest's output folders, and after
  # this loop it would point the DLL targets at out/imgdump/gen_default (ONE module: the last of the loop). Put the
  # regen.sh version back afterwards, or the next configure builds a single core (bitten 2026-11-03).
  [ -f generated/rexglue.cmake ] && cp -f generated/rexglue.cmake out/imgdump/rexglue.cmake.keep
  for m in "${MODS[@]}"; do
    inc="[]"; [ -f "${m}_functions.toml" ] && inc="[\"${m}_functions.toml\"]"
    cat > .imgdump_solo.toml <<TOML
[project]
name = "futari"
sdk_version = "0.8.0.0"
game_root = "assets"

[entrypoint]
file_path = "assets/default.xex"
out_directory_path = "out/imgdump/gen_default"
includes = ["futari_functions.toml"]

[[modules]]
guest_path = "x"
file_path = "assets/media/Module/$m.xex"
out_directory_path = "out/imgdump/gen_$m"
includes = $inc
TOML
    REXGLUE_DUMP_IMAGE="out/imgdump/default_image.bin" "$REXGLUE" codegen .imgdump_solo.toml > "out/imgdump/codegen_$m.log" 2>&1 \
      || { echo "codegen $m FAILED (out/imgdump/codegen_$m.log)" >&2; rm -f .imgdump_solo.toml; exit 1; }
    mv -f "out/imgdump/default_image.$m.bin" "out/${m}_image.bin"
    echo "  [image] $m"
  done
  mv -f out/imgdump/default_image.bin out/default_image.bin
  rm -f .imgdump_solo.toml
  [ -f out/imgdump/rexglue.cmake.keep ] && mv -f out/imgdump/rexglue.cmake.keep generated/rexglue.cmake
  echo "  [image] default"
else
  echo "[images] out/*_image.bin present (--force to redo)"
fi

rm -rf out/renut_shader_dump
# PYTHON / PYTHON_ARGS: the interpreter when it is not "python3" on PATH (build.ps1 passes the one it found, e.g.
# PYTHON="C:/Program Files/Python312/python.exe", or PYTHON=py PYTHON_ARGS=-3).
"${PYTHON:-python3}" ${PYTHON_ARGS:-} tools/renut/renut_dump_shaders.py
