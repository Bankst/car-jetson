#!/bin/bash
set -euo pipefail

IMAGE="${1:-base}"
DEPLOY="build/tmp/deploy/images/jetson-xavier-nx-a203"
TARBALL="$DEPLOY/banks-jetson-image-${IMAGE}-jetson-xavier-nx-a203.rootfs.tegraflash.tar.gz"
FLASH_DIR="/tmp/flash-$$"

if [ ! -f "$TARBALL" ]; then
    echo "Not found: $TARBALL"
    exit 1
fi

echo "Extracting to $FLASH_DIR ..."
mkdir -p "$FLASH_DIR"
tar xzf "$TARBALL" -C "$FLASH_DIR"

echo ""
echo "Ready to flash. Put module in recovery mode (short FC REC, power cycle)."
echo "Press Enter to start flashing, or Ctrl-C to abort."
read -r

cd "$FLASH_DIR"
./initrd-flash --erase-nvme
cd -

echo ""
read -rp "Delete $FLASH_DIR? [y/N] " confirm
if [ "$confirm" = "y" ] || [ "$confirm" = "Y" ]; then
    rm -rf "$FLASH_DIR"
    echo "Deleted."
else
    echo "Kept at $FLASH_DIR"
fi
