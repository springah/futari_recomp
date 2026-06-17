#!/usr/bin/env bash
# Assemble a self-contained Futari.app from the mac-arm64 build + extracted assets.
# Output: ~/Desktop/projects/Futari.app
set -euo pipefail

PROJ="$HOME/Desktop/projects"
BUILD="$PROJ/futari-mac/futari_recomp/out/build/mac-arm64-release"
SDK="$PROJ/futari-mac/futari_recomp/thirdparty/rexglue-sdk"
ASSETS_SRC="$PROJ/Mushihimesama-Futari-PC-v1.2.0"
APP="$PROJ/Futari.app"
C="$APP/Contents"

echo "[1/7] fresh bundle skeleton"
rm -rf "$APP"
mkdir -p "$C/MacOS" "$C/lib" "$C/Resources/vulkan/icd.d"

echo "[2/7] executable + colocated dylibs"
cp "$BUILD/futari" "$C/MacOS/"
cp "$BUILD"/libfutari_*.dylib "$C/MacOS/"
cp "$BUILD/librexruntime.dylib" "$C/MacOS/"
cp "$BUILD/libTracyClient.dylib" "$C/MacOS/"

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

echo "[4/7] game data (assets + dlc) — ~1.1GB, copying..."
ditto "$ASSETS_SRC/assets" "$C/MacOS/assets"
[ -d "$ASSETS_SRC/dlc" ] && ditto "$ASSETS_SRC/dlc" "$C/MacOS/dlc"

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
cat > "$C/Info.plist" <<'PLIST'
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
    <key>CFBundleShortVersionString</key>    <string>1.3.2</string>
    <key>CFBundleVersion</key>               <string>1.3.2</string>
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
