#!/usr/bin/env bash
# Systematic per-module function-hint pass for the futari game modules.
#
# ReXGlue's analyzer misses functions that are only reached by indirect calls
# (no PDATA/static edge) -> runtime "[FATAL] Call to invalid or unregistered
# function at 0x88xxxxxx". XenonRecomp's analyzer covers the whole code section
# (PDATA + bl-scan + linear gap-fill), so its function map is a richer reference.
#
# For each module this script:
#   1. Runs XenonRecomp on the module XEX to get a reference function map.
#      ABI save/rest addresses are left unspecified (=0) on purpose: they are an
#      optimization, not required for FUNCTION DISCOVERY, and ppc_func_mapping.cpp
#      is written before the per-function recompile that would warn about them.
#   2. Codegens the module with NO hints -> ReXGlue's base registered set.
#   3. diff (XenonRecomp - ReXGlue) = candidate missing functions.
#   4. Bulk-seeds every candidate and codegens; ReXGlue logs
#      "Function 0x.. has no blocks - generating stub" for addresses that are
#      data/misidentified (XenonRecomp gap-fill over .long 0x0 etc).
#   5. Keeps only the candidates that decoded to REAL instruction blocks and
#      writes <module>_functions.toml (scripts/regen.sh auto-includes it).
#   6. Re-codegens with just those and asserts 0 module stubs / 0 module
#      unresolved errors.
#
# 0x82xxxxxx warnings in the logs are the shell (entrypoint) codegen and are
# pre-existing; this script only ever acts on 0x88xxxxxx module addresses.
#
# REQUIRES XenonRecomp, which is NOT a submodule of this repo -- it is only
# needed to regenerate the *_functions.toml hints, which are already committed,
# so a normal build never touches it. To run this script, clone and build it
# yourself and point XENON at the binary:
#
#   git clone --recursive https://github.com/hedge-dev/XenonRecomp thirdparty/XenonRecomp
#   cmake -S thirdparty/XenonRecomp -B thirdparty/XenonRecomp/build && \
#     cmake --build thirdparty/XenonRecomp/build
#
# Usage: scripts/build_module_hints.sh [module ...]   (default: all 8)
#        run from repo root. Override XENON / CTXHDR / REXGLUE to point at
#        binaries elsewhere.
set -euo pipefail
cd "$(dirname "$0")/.."

XENON="${XENON:-./thirdparty/XenonRecomp/build/XenonRecomp/XenonRecomp.exe}"
CTXHDR="${CTXHDR:-thirdparty/XenonRecomp/XenonUtils/ppc_context.h}"
REXGLUE="${REXGLUE:-./thirdparty/rexglue-sdk/out/win-amd64/rexglue.exe}"

# Preflight: fail with something actionable rather than a bare "not found" on
# the first codegen call. XenonRecomp is a manual, non-submodule dependency.
if [ ! -x "$XENON" ]; then
  echo "ERROR: XenonRecomp not found at: $XENON" >&2
  echo "       It is NOT a submodule -- see the header of this script for how to" >&2
  echo "       build it, or set XENON=/path/to/XenonRecomp." >&2
  echo "       You do not need it for a normal build: the *_functions.toml hints" >&2
  echo "       this script produces are already committed." >&2
  exit 1
fi
if [ ! -f "$CTXHDR" ]; then
  echo "ERROR: ppc_context.h not found at: $CTXHDR (set CTXHDR=...)" >&2
  exit 1
fi
if [ ! -x "$REXGLUE" ]; then
  echo "ERROR: rexglue not found at: $REXGLUE" >&2
  echo "       Build the SDK CLI first (see README), or set REXGLUE=/path/to/rexglue." >&2
  exit 1
fi
WORK="scripts/module_maps"
ALL_MODS=(reco_normal reco_abnormal palm_normal palm_abnormal hirow_normal hirow_abnormal kiniro_normal kiniro_abnormal)
if [ "$#" -gt 0 ]; then MODS=("$@"); else MODS=("${ALL_MODS[@]}"); fi

mkdir -p "$WORK"

# Emit a one-module manifest. $1=module $2=includes-toml-array
solo_manifest() {
  printf '[project]\nname = "futari"\nsdk_version = "0.8.0.0"\ngame_root = "assets"\n\n[entrypoint]\nfile_path = "assets/default.xex"\nout_directory_path = "generated/default"\nincludes = ["futari_functions.toml"]\n\n[[modules]]\nguest_path = "x"\nfile_path = "assets/media/Module/%s.xex"\nout_directory_path = "generated/%s"\nincludes = %s\n' "$1" "$1" "$2" > .regen_solo.toml
}

for m in "${MODS[@]}"; do
  echo "==== $m ===="
  xex="assets/media/Module/${m}.xex"
  [ -f "$xex" ] || { echo "  [skip] $xex not found"; continue; }

  # 1. XenonRecomp reference map.
  mkdir -p "$WORK/$m"
  printf '[main]\nfile_path = "../../%s"\nout_directory_path = "%s"\nskip_lr = false\nskip_msr = false\nctr_as_local = false\nxer_as_local = false\nreserved_as_local = false\ncr_as_local = false\nnon_argument_as_local = false\nnon_volatile_as_local = false\n' "$xex" "$m" > "$WORK/${m}.toml"
  "$XENON" "$WORK/${m}.toml" "$CTXHDR" > "$WORK/${m}_xenon.log" 2>&1 || true
  grep -oE "0x88[0-9A-Fa-f]{6}" "$WORK/$m/ppc_func_mapping.cpp" | tr 'a-f' 'A-F' | sort -u > "$WORK/${m}_xenon.txt"
  echo "  xenon funcs: $(wc -l < "$WORK/${m}_xenon.txt")"

  # 2. ReXGlue base set (no module hints).
  solo_manifest "$m" "[]"
  "$REXGLUE" codegen .regen_solo.toml > "$WORK/${m}_base.log" 2>&1
  grep -oE "SetFunction\(0x88[0-9A-Fa-f]{6}" "generated/$m/futari_register.cpp" | grep -oE "0x88[0-9A-Fa-f]{6}" | tr 'a-f' 'A-F' | sort -u > "$WORK/${m}_rex.txt"
  echo "  rex base funcs: $(wc -l < "$WORK/${m}_rex.txt")"

  # 3. Candidates.
  comm -23 "$WORK/${m}_xenon.txt" "$WORK/${m}_rex.txt" > "$WORK/${m}_cand.txt"
  echo "  candidates: $(wc -l < "$WORK/${m}_cand.txt")"

  # 4. Bulk-seed + codegen + collect stubs.
  { echo "[functions]"; awk '{print "\""$1"\" = {}"}' "$WORK/${m}_cand.txt"; } > "${m}_functions.toml"
  solo_manifest "$m" "[\"${m}_functions.toml\"]"
  "$REXGLUE" codegen .regen_solo.toml > "$WORK/${m}_bulk.log" 2>&1
  grep "has no blocks - generating stub" "$WORK/${m}_bulk.log" | grep -oE "0x88[0-9A-Fa-f]{6}" | tr 'a-f' 'A-F' | sort -u > "$WORK/${m}_stubs.txt"
  echo "  stubs (data): $(wc -l < "$WORK/${m}_stubs.txt")"

  # 5. Keep real-block candidates.
  comm -23 "$WORK/${m}_cand.txt" "$WORK/${m}_stubs.txt" > "$WORK/${m}_real.txt"
  real=$(wc -l < "$WORK/${m}_real.txt")
  { echo "# Function-entry hints for the ${m} game module (base 0x88000000)."; \
    echo "# Auto-derived by scripts/build_module_hints.sh: XenonRecomp reference map"; \
    echo "# minus ReXGlue-registered, filtered to the addresses ReXGlue decodes to"; \
    echo "# real instruction blocks (data/gap-fill addresses stub out and are dropped)."; \
    echo "[functions]"; \
    awk '{print "\""$1"\" = {}"}' "$WORK/${m}_real.txt"; } > "${m}_functions.toml"
  echo "  REAL seeds written: $real"

  # 6. Validate.
  solo_manifest "$m" "[\"${m}_functions.toml\"]"
  "$REXGLUE" codegen .regen_solo.toml > "$WORK/${m}_final.log" 2>&1
  fs=$(grep "has no blocks" "$WORK/${m}_final.log" | grep -c "0x88" || true)
  fu=$(grep -iE "unresolved" "$WORK/${m}_final.log" | grep -c "0x88" || true)
  echo "  validate: module stubs=$fs unresolved=$fu"
  [ "$fs" = "0" ] && [ "$fu" = "0" ] || echo "  [WARN] $m did not validate clean"
done

rm -f .regen_solo.toml
echo "Done. Per-module <module>_functions.toml written; rebuild via README/regen flow."
