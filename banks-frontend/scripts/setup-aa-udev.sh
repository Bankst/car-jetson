#!/bin/bash
# Install udev rules for Android Auto AOAP devices (Google VID 18d1).
# Run once on any host or target that connects phones via USB.
set -euo pipefail

RULE_FILE="/etc/udev/rules.d/51-android-auto.rules"

cat > "$RULE_FILE" <<'EOF'
# Android Open Accessory Protocol (AOAP) — used by Android Auto
SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="2d00", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", ATTR{idProduct}=="2d01", MODE="0666"
# Google devices pre-AOAP handshake (common Pixel VID/PIDs)
SUBSYSTEM=="usb", ATTR{idVendor}=="18d1", MODE="0664", GROUP="plugdev"
EOF

udevadm control --reload-rules
udevadm trigger

echo "Installed $RULE_FILE"
