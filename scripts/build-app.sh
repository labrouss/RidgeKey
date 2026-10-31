#!/bin/bash
# Build RidgeKey.app (SwiftUI settings app) from app/.
# Works with Xcode or just the Command Line Tools (swift + macOS SDK).
#
#   bash scripts/build-app.sh [OUTPUT.app]      default: app/build/RidgeKey.app
#   open app/build/RidgeKey.app                 talks to the installed ridgekeyd
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$REPO/app/build/RidgeKey.app}"
VER="$(sed -n 's/^#define RK_VERSION *"\(.*\)"/\1/p' "$REPO/src/common/ridgekey-protocol.h")"

command -v swift >/dev/null || { echo "swift not found - install the Xcode Command Line Tools"; exit 1; }

echo "==> swift build (RidgeKey.app $VER)"
( cd "$REPO/app" && swift build -c release )
BIN="$(cd "$REPO/app" && swift build -c release --show-bin-path)/RidgeKey"
[ -x "$BIN" ] || { echo "build produced no binary"; exit 1; }

echo "==> assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"
cp "$BIN" "$OUT/Contents/MacOS/RidgeKey"

ICON_KEY=""
# App icon: assets/ridgekey-icon.png (1024x1024, the key emblem on Apple's icon grid) -> .icns.
ICON_SRC="$REPO/assets/ridgekey-icon.png"
if command -v sips >/dev/null && command -v iconutil >/dev/null && [ -f "$ICON_SRC" ]; then
  T="$(mktemp -d)"
  mkdir -p "$T/RidgeKey.iconset"
  for s in 16 32 128 256 512; do
    sips -z $s $s "$ICON_SRC" --out "$T/RidgeKey.iconset/icon_${s}x${s}.png" >/dev/null 2>&1
    d=$((s * 2))
    sips -z $d $d "$ICON_SRC" --out "$T/RidgeKey.iconset/icon_${s}x${s}@2x.png" >/dev/null 2>&1
  done
  if iconutil -c icns "$T/RidgeKey.iconset" -o "$OUT/Contents/Resources/RidgeKey.icns" 2>/dev/null; then
    ICON_KEY="<key>CFBundleIconFile</key><string>RidgeKey</string>"
  fi
  rm -rf "$T"
fi
# The full logo, shown at the top of the About screen.
[ -f "$REPO/assets/ridgekey-logo.png" ] && cp "$REPO/assets/ridgekey-logo.png" "$OUT/Contents/Resources/RidgeKeyLogo.png"

cat > "$OUT/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>RidgeKey</string>
  <key>CFBundleDisplayName</key><string>RidgeKey</string>
  <key>CFBundleIdentifier</key><string>io.github.labrouss.ridgekey.app</string>
  <key>CFBundleExecutable</key><string>RidgeKey</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VER</string>
  <key>CFBundleVersion</key><string>$VER</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSAppleEventsUsageDescription</key><string>RidgeKey asks for an administrator password to turn fingerprint for sudo on or off.</string>
  $ICON_KEY
</dict>
</plist>
EOF

codesign --force --sign - "$OUT" >/dev/null 2>&1 || echo "    (ad-hoc signing failed - the app still runs locally)"
"$OUT/Contents/MacOS/RidgeKey" --version
echo "Built $OUT"
