#!/usr/bin/env bash
# Builds Corbata PDF for macOS and packages it as dist/Corbata-PDF-macos.dmg.
#
# Needs: Xcode command line tools, CMake, Qt 6.8 for macOS with the Qt PDF
# module, and vcpkg with qpdf installed for this Mac's architecture:
#   vcpkg install qpdf:arm64-osx      (Apple Silicon)
#   vcpkg install qpdf:x64-osx        (Intel)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
QT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.8.3/macos}"
VCPKG="${VCPKG_ROOT:-$HOME/vcpkg}"
if [[ "$(uname -m)" == "arm64" ]]; then TRIPLET=arm64-osx; else TRIPLET=x64-osx; fi
BUILD="$ROOT/build-mac"

# App icon (.icns) from the 1024px PNG.
ICONSET="$BUILD/AppIcon.iconset"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z $s $s "$ROOT/resources/AppIcon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s*2)) $((s*2)) "$ROOT/resources/AppIcon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$ROOT/resources/AppIcon.icns"

cmake -S "$ROOT" -B "$BUILD" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="$TRIPLET" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure

mkdir -p "$ROOT/dist"
rm -rf "$ROOT/dist/Corbata PDF.app" "$ROOT/dist/Corbata-PDF-macos.dmg"
cp -R "$BUILD/Corbata PDF.app" "$ROOT/dist/"
"$QT_DIR/bin/macdeployqt" "$ROOT/dist/Corbata PDF.app" -always-overwrite
# Ad-hoc signature so Apple Silicon will launch it (not notarized).
codesign --force --deep --sign - "$ROOT/dist/Corbata PDF.app"
hdiutil create -volname "Corbata PDF" -srcfolder "$ROOT/dist/Corbata PDF.app" -ov -format UDZO \
  "$ROOT/dist/Corbata-PDF-macos.dmg"
echo "Done: $ROOT/dist/Corbata-PDF-macos.dmg"
