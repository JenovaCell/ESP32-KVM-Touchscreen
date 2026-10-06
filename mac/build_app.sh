#!/bin/bash
# Builds KVMBridge.app (unsigned apart from an ad-hoc signature).
set -euo pipefail
cd "$(dirname "$0")"

VERSION=$(tr -d '[:space:]' < ../VERSION)
swift build -c release

APP=build/KVMBridge.app
rm -rf build
mkdir -p "$APP/Contents/MacOS"
cp .build/release/KVMBridge "$APP/Contents/MacOS/KVMBridge"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>KVMBridge</string>
  <key>CFBundleIdentifier</key><string>io.github.jenovacell.kvmbridge</string>
  <key>CFBundleExecutable</key><string>KVMBridge</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>LSUIElement</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>KVMBridge connects to your KVM device over Bluetooth to send keystrokes.</string>
</dict>
</plist>
PLIST

codesign --force --sign - "$APP"
echo "Built $APP"
