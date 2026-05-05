#!/bin/bash
set -e
cd "$(dirname "$0")/files"
for dtb in *.dtb; do
    echo "Patching $dtb..."
    dts="${dtb%.dtb}.dts"
    dtc -I dtb -O dts "$dtb" -o "$dts" 2>/dev/null
    sed -i 's|bootargs = "console=ttyTCU0,115200"|bootargs = "console=ttyTCU0,115200 console=ttyTHS2,115200"|' "$dts"
    sed -i '/serial@c280000 {/,/};/{s/status = "disabled"/status = "okay"/}' "$dts"
    dtc -I dts -O dtb "$dts" -o "$dtb" 2>/dev/null
    rm "$dts"
    echo "  done"
done
echo "All DTBs patched"
