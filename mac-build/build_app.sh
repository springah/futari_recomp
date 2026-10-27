#!/usr/bin/env bash
# Assemble a self-contained Futari.app from the mac-arm64 build + extracted assets.
#
# This is the maintainer's packaging script, and its defaults describe one
# machine's layout: it expects the repo, the SDK, and a directory of extracted
# game data to sit side by side under $PROJ. Every path is overridable by
# environment variable, so it works anywhere you point it:
#
#   PROJ=...        parent directory holding the asset dirs (and the .app)
#   REPO=...        this checkout (default: the one this script lives in)
#   BUILD=...       cmake build dir containing futari + libfutari_*.dylib
#                   (default: $REPO/out/build/mac-arm64-release)
#   SDK=...         SDK source tree (default: $REPO/thirdparty/rexglue-sdk;
#                   its dylibs come from $SDK/out/mac-arm64/)
#   ASSETS_SRC=...  directory containing assets/ and (optionally) dlc/
#   APP=...         output bundle path
#   VER=...         CFBundleShortVersionString (keep in sync with CMakeLists.txt)
#   SKIP_ASSETS=1   build the bundle without game data (ASSETS_SRC not needed)
#   ICON_SRC=...    .ico to turn into the app icon (default: $REPO/src/futari.ico)
#
# The bundle it produces contains YOUR game data. It is for your own machines;
# do not redistribute it. See PORTS.md for what a public bundle may contain.
set -euo pipefail

PROJ="${PROJ:-$HOME/Desktop/projects}"
VER="${VER:-1.5.3}"
# The checkout is wherever this script lives; the build dir and the SDK are
# derived from it so one bundle never mixes two trees.
REPO="${REPO:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
BUILD="${BUILD:-$REPO/out/build/mac-arm64-release}"
# SDK source tree. Its runtime dylibs land in $SDK/out/mac-arm64/.
# THE VENDORED TREE WINS: builds link thirdparty/rexglue-sdk, so the bundle must
# ship that same SDK's dylibs. $REXSDK_DIR is a persistent user-profile value pointing
# at a shared sibling checkout for the OTHER ports, and honouring it here shipped
# a v0.8.5.1 librexruntime.dylib next to a v0.9.3.1 futari in v1.4.1 -- the app
# died at launch with "Symbol not found: __Z28FLAGS_metadata_root_storage_v".
# Set SDK=... explicitly to override.
SDK="${SDK:-$REPO/thirdparty/rexglue-sdk}"
# Assets default: an env-provided ASSETS_SRC always wins. Otherwise pick the
# NEWEST Mushihimesama-Futari-PC-v* dir under $PROJ (highest version), so the
# script doesn't pin a stale release. bash-3.2 safe; set -u safe (an unmatched
# glob must not expand to a literal and crash). We list candidates with find and
# take the highest with sort -V; no nullglob/associative arrays/globstar needed.
# Not needed at all for a SKIP_ASSETS=1 bundle.
if [ -z "${ASSETS_SRC:-}" ] && [ "${SKIP_ASSETS:-0}" != "1" ]; then
  ASSETS_SRC="$(find "$PROJ" -maxdepth 1 -type d -name 'Mushihimesama-Futari-PC-v*' \
                  2>/dev/null | sort -V | tail -1)"
  if [ -z "$ASSETS_SRC" ]; then
    echo "ERROR: no Mushihimesama-Futari-PC-v* directory found under $PROJ" >&2
    echo "       set ASSETS_SRC=/path/to/assets-dir to override." >&2
    exit 1
  fi
fi
APP="${APP:-$PROJ/Futari.app}"
C="$APP/Contents"

# Copy a file from the first place it exists: the game build dir (old in-tree
# layout colocated the SDK dylibs there) or the SDK's own out/mac-arm64/ dir
# (where the shared external SDK emits librexruntime.dylib / libTracyClient.dylib).
copy_first() {
  local name="$1" dest="$2" c
  for c in "$BUILD/$name" "$SDK/out/mac-arm64/$name"; do
    if [ -f "$c" ]; then cp "$c" "$dest"; return 0; fi
  done
  echo "  ERROR: $name not found in \$BUILD or \$SDK/out/mac-arm64" >&2; return 1
}

echo "[1/8] fresh bundle skeleton"
rm -rf "$APP"
mkdir -p "$C/MacOS" "$C/lib" "$C/Resources/vulkan/icd.d"

echo "[2/8] executable + colocated dylibs"
cp "$BUILD/futari" "$C/MacOS/"
cp "$BUILD"/libfutari_*.dylib "$C/MacOS/"
copy_first librexruntime.dylib "$C/MacOS/"
copy_first libTracyClient.dylib "$C/MacOS/"

# GPU plugin (SDK v0.9 split the renderer out of the runtime; without this the
# app starts and then dies with "Failed to load GPU plugin 'xenos'").
# NOTE THE RENAME: CMake builds it as librexgpu-xenos.dylib, but the SDK's
# PluginFileName() asks for ".so" on every non-Windows platform, macOS included
# (src/system/gpu_plugin_loader.cpp) -- even though dynlib_posix.cpp does know
# about .dylib. dlopen ignores the extension, so staging it under the name the
# loader wants works. Remove the rename once the SDK learns .dylib on Apple.
for c in "$BUILD/librexgpu-xenos.dylib" "$SDK/out/mac-arm64/librexgpu-xenos.dylib"; do
  if [ -f "$c" ]; then cp "$c" "$C/MacOS/librexgpu-xenos.so"; break; fi
done
[ -f "$C/MacOS/librexgpu-xenos.so" ] || { echo "  ERROR: librexgpu-xenos.dylib not found" >&2; exit 1; }

echo "[3/8] bundled MoltenVK (self-contained, no Homebrew dependency)"
# Resolve the real dylib (Homebrew symlink) so the copy is standalone.
MVK="$(readlink -f /opt/homebrew/lib/libMoltenVK.dylib)"
cp "$MVK" "$C/lib/libMoltenVK.dylib"

# Third-party licences must travel with the binary: the runtime statically links
# FFmpeg and libmspack (LGPL-2.1), and MoltenVK here is Apache-2.0. Fail the
# build rather than ship a bundle with no licence text -- every macOS bundle
# before this one shipped none.
# The notices are regenerated from the SDK tree every time (into a temp file,
# so a failed run cannot leave a truncated one behind) and compared with the
# committed copy: if they differ the tree moved and the committed file is stale.
notices_tmp="$(mktemp)"
if ! SDK="$SDK" bash "$REPO/scripts/gen_third_party_notices.sh" > "$notices_tmp"; then
  rm -f "$notices_tmp"
  echo "  ERROR: gen_third_party_notices.sh failed; not shipping a bundle without licence texts." >&2
  exit 1
fi
if ! cmp -s "$notices_tmp" "$REPO/THIRD-PARTY-NOTICES.txt"; then
  echo "  NOTE: THIRD-PARTY-NOTICES.txt was stale against the SDK tree -- regenerated. Commit it." >&2
  mv "$notices_tmp" "$REPO/THIRD-PARTY-NOTICES.txt"
else
  rm -f "$notices_tmp"
fi
cp "$REPO/THIRD-PARTY-NOTICES.txt" "$C/Resources/"
MVK_LIC="$(dirname "$MVK")/../share/doc/molten-vk/LICENSE"
[ -f "$MVK_LIC" ] || MVK_LIC="$(brew --prefix molten-vk 2>/dev/null)/LICENSE"
if [ -f "$MVK_LIC" ]; then
  cp "$MVK_LIC" "$C/Resources/LICENSE-MoltenVK.txt"
else
  echo "  ERROR: MoltenVK LICENSE not found (looked beside the dylib and in brew --prefix)." >&2
  echo "         Apache-2.0 requires it to ship. Copy it to Contents/Resources/ and re-run." >&2
  exit 1
fi
# ICD json: library_path "../../../lib/libMoltenVK.dylib" resolves from
# Resources/vulkan/icd.d/ -> Contents/lib/libMoltenVK.dylib
cat > "$C/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'JSON'
{
    "file_format_version" : "1.0.0",
    "ICD": {
        "library_path": "../../../lib/libMoltenVK.dylib",
        "api_version" : "1.4.0",
        "is_portability_driver" : true
    }
}
JSON

if [ "${SKIP_ASSETS:-0}" = "1" ]; then
  echo "[4/8] game data — SKIPPED (SKIP_ASSETS=1, verification build)"
else
  echo "[4/8] game data (assets + dlc) — ~1.1GB, copying..."
  ditto "$ASSETS_SRC/assets" "$C/MacOS/assets"
  [ -d "$ASSETS_SRC/dlc" ] && ditto "$ASSETS_SRC/dlc" "$C/MacOS/dlc"
fi

echo "[5/8] icon"
# The repo's own identification icon (see NOTICE.md), so this step does not
# depend on the asset dump.
ICON_SRC="${ICON_SRC:-$REPO/src/futari.ico}"
WORK="$(mktemp -d)"
if sips -s format png "$ICON_SRC" --out "$WORK/base.png" >/dev/null 2>&1; then
  ICONSET="$WORK/AppIcon.iconset"; mkdir -p "$ICONSET"
  for s in 16 32 64 128 256 512; do
    sips -z $s $s "$WORK/base.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null 2>&1 || true
    d=$((s*2)); sips -z $d $d "$WORK/base.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null 2>&1 || true
  done
  iconutil -c icns "$ICONSET" -o "$C/Resources/AppIcon.icns" 2>/dev/null \
    && echo "  icon ok" || echo "  icon build failed (non-fatal)"
else
  echo "  could not read .ico (non-fatal, app gets generic icon)"
fi
rm -rf "$WORK"

echo "[6/8] Info.plist"
cat > "$C/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                  <string>Futari</string>
    <key>CFBundleDisplayName</key>           <string>Mushihimesama Futari</string>
    <key>CFBundleIdentifier</key>            <string>com.springah.futari</string>
    <key>CFBundleExecutable</key>            <string>futari</string>
    <key>CFBundleIconFile</key>              <string>AppIcon</string>
    <key>CFBundlePackageType</key>           <string>APPL</string>
    <key>CFBundleInfoDictionaryVersion</key> <string>6.0</string>
    <key>CFBundleShortVersionString</key>    <string>${VER}</string>
    <key>CFBundleVersion</key>               <string>${VER}</string>
    <key>LSMinimumSystemVersion</key>        <string>13.0</string>
    <key>NSHighResolutionCapable</key>       <true/>
    <key>LSApplicationCategoryType</key>     <string>public.app-category.games</string>
</dict>
</plist>
PLIST

echo "[7/8] ad-hoc code signature (required for arm64 launch)"
# An unsigned arm64 app is killed at launch, so a codesign failure is fatal.
codesign --force --deep --sign - "$APP"

echo "[8/8] version consistency check"
# Every shipped component must report the same SDK. A mismatch here is what
# broke v1.4.1's first mac bundle, and it fails at launch, not at build time.
# (|| true: a missing marker must reach the check below, not abort silently.)
exe_v="$(strings "$C/MacOS/futari" | grep -oE 'rexglue-v[0-9][0-9.]*' | sort -u | head -1 || true)"
rt_v="$(strings "$C/MacOS/librexruntime.dylib" | grep -oE 'rexglue-v[0-9][0-9.]*' | sort -u | head -1 || true)"
echo "  futari:            ${exe_v:-?}"
echo "  librexruntime:     ${rt_v:-?}"
if [ -z "$exe_v" ] || [ -z "$rt_v" ]; then
  echo "  ERROR: could not read the SDK version marker from one of the binaries." >&2
  exit 1
fi
if [ "$exe_v" != "$rt_v" ]; then
  echo "  ERROR: SDK mismatch between the executable and the runtime -- this bundle will not launch." >&2
  exit 1
fi
echo "  match"

echo ""
echo "DONE -> $APP"
du -sh "$APP"
