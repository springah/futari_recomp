#!/usr/bin/env bash
# Assemble a self-contained Futari.app from the mac-arm64 build + extracted assets.
# Output: ~/Desktop/projects/Futari.app
set -euo pipefail

PROJ="${PROJ:-$HOME/Desktop/projects}"
VER="${VER:-1.4.0}"
BUILD="${BUILD:-$PROJ/futari-mac/futari_recomp/out/build/mac-arm64-release}"
# SDK source tree (the external shared rexglue-sdk). Defaults to $REXSDK_DIR, else
# the legacy in-tree submodule path. Its runtime dylibs land in $SDK/out/mac-arm64/.
SDK="${SDK:-${REXSDK_DIR:-$PROJ/futari-mac/futari_recomp/thirdparty/rexglue-sdk}}"
# Assets default: an env-provided ASSETS_SRC always wins. Otherwise pick the
# NEWEST Mushihimesama-Futari-PC-v* dir under $PROJ (highest version), so the
# script doesn't pin a stale release. bash-3.2 safe; set -u safe (an unmatched
# glob must not expand to a literal and crash). We list candidates with find and
# take the highest with sort -V; no nullglob/associative arrays/globstar needed.
if [ -z "${ASSETS_SRC:-}" ]; then
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

echo "[1/7] fresh bundle skeleton"
rm -rf "$APP"
mkdir -p "$C/MacOS" "$C/lib" "$C/Resources/vulkan/icd.d"

echo "[2/7] executable + colocated dylibs"
cp "$BUILD/futari" "$C/MacOS/"
cp "$BUILD"/libfutari_*.dylib "$C/MacOS/"
copy_first librexruntime.dylib "$C/MacOS/"
copy_first libTracyClient.dylib "$C/MacOS/"

echo "[3/7] bundled MoltenVK (self-contained, no Homebrew dependency)"
# Resolve the real dylib (Homebrew symlink) so the copy is standalone.
MVK="$(readlink -f /opt/homebrew/lib/libMoltenVK.dylib)"
cp "$MVK" "$C/lib/libMoltenVK.dylib"
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
  echo "[4/7] game data — SKIPPED (SKIP_ASSETS=1, verification build)"
else
  echo "[4/7] game data (assets + dlc) — ~1.1GB, copying..."
  ditto "$ASSETS_SRC/assets" "$C/MacOS/assets"
  [ -d "$ASSETS_SRC/dlc" ] && ditto "$ASSETS_SRC/dlc" "$C/MacOS/dlc"
fi

echo "[5/7] icon"
ICON_SRC="$ASSETS_SRC/icon1.ico"
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

echo "[6/7] Info.plist"
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

echo "[7/7] ad-hoc code signature (required for arm64 launch)"
codesign --force --deep --sign - "$APP" 2>&1 | tail -2 || echo "  (codesign warning, usually fine)"

echo ""
echo "DONE -> $APP"
du -sh "$APP"
