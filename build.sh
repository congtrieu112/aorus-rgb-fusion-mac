#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
APP_NAME="AORUS RGB Fusion.app"
BUILD_DIR="$DIR/build"
APP_DIR="$BUILD_DIR/$APP_NAME"
MACOS_DIR="$APP_DIR/Contents/MacOS"
RES_DIR="$APP_DIR/Contents/Resources"
INSTALL_APP=1

for arg in "$@"; do
  case "$arg" in
    --no-install) INSTALL_APP=0 ;;
  esac
done

mkdir -p "$BUILD_DIR"

echo "==> Compiling aorus-led CLI..."
clang -O2 -Wall -Wextra -I"$DIR/src" "$DIR/src/aorus-led.c" -o "$BUILD_DIR/aorus-led" \
  -framework IOKit -framework CoreFoundation

echo "==> Creating .app bundle..."
rm -rf "$APP_DIR"
mkdir -p "$MACOS_DIR" "$RES_DIR"

echo "==> Compiling AORUS RGB Fusion GUI..."
clang -O2 -fobjc-arc -Wall -Wextra -I"$DIR/src" "$DIR/src/app.m" -o "$MACOS_DIR/AORUS RGB Fusion" \
  -framework Cocoa -framework WebKit -framework IOKit -framework CoreFoundation

cp "$BUILD_DIR/aorus-led" "$MACOS_DIR/aorus-led"
cp "$DIR/src/ui.html" "$RES_DIR/ui.html"

cat > "$APP_DIR/Contents/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>AORUS RGB Fusion</string>
  <key>CFBundleIdentifier</key>
  <string>com.aorus.rgbfusion.mac</string>
  <key>CFBundleName</key>
  <string>AORUS RGB Fusion</string>
  <key>CFBundleDisplayName</key>
  <string>AORUS RGB Fusion</string>
  <key>CFBundleVersion</key>
  <string>1.0.0</string>
  <key>CFBundleShortVersionString</key>
  <string>1.0.0</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>CFBundleIconFile</key>
  <string>AppIcon</string>
</dict>
</plist>
EOF

echo "==> Generating AppIcon.icns..."
ICONSET="$BUILD_DIR/.AppIcon.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
python3 - "$ICONSET/icon_512x512.png" << 'PYEOF'
import sys, struct, zlib, math

W = H = 512
raw = bytearray()
cx, cy = W / 2, H / 2
for y in range(H):
    raw.append(0)
    for x in range(W):
        rad = 110
        dx = max(abs(x - cx) - (W/2 - rad), 0)
        dy = max(abs(y - cy) - (H/2 - rad), 0)
        dist = math.hypot(dx, dy)
        if dist > rad:
            raw.extend((0, 0, 0, 0))
            continue
        dcenter = math.hypot(x - cx, y - cy) / 256.0
        glow = max(0.0, 1.0 - dcenter * 1.15) ** 2.2
        r = int(min(255, 16 + glow * 130))
        g = int(min(255, 16 + glow * 52))
        b = int(min(255, 22 + glow * 8))
        if rad - 10 <= dist <= rad - 2:
            r, g, b = 255, 100, 0
        nx = (x - cx) / 140.0
        ny = (y - cy) / 140.0
        in_emblem = (
            (abs(nx) + abs(ny) < 0.92 and abs(nx) + abs(ny) > 0.42 and ny < 0.55)
            or (abs(nx) < 0.25 and abs(ny + 0.1) < 0.25)
        )
        if in_emblem:
            r, g, b = 255, 140, 0
        raw.extend((r, g, b, 255))

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
png += chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
PYEOF

for sz in 16 32 128 256 512; do
  sips -z $sz $sz "$ICONSET/icon_512x512.png" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null
  sz2=$((sz * 2))
  if [ $sz2 -le 512 ]; then
    sips -z $sz2 $sz2 "$ICONSET/icon_512x512.png" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null
  fi
done
iconutil -c icns "$ICONSET" -o "$RES_DIR/AppIcon.icns"
rm -rf "$ICONSET"

if [ "$INSTALL_APP" -eq 1 ]; then
  echo "==> Installing to /Applications/$APP_NAME ..."
  rm -rf "/Applications/$APP_NAME"
  cp -R "$APP_DIR" "/Applications/$APP_NAME"
fi

echo "==> Build complete: $APP_DIR"
ls -lh "$MACOS_DIR/AORUS RGB Fusion" "$MACOS_DIR/aorus-led"
