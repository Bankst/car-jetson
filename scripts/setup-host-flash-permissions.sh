#!/bin/bash
# One-time host setup for unprivileged initrd-flash.
# This is also auto-run by initrd-flash on first use — this script
# is for pre-setup or verification.
set -euo pipefail

echo "Checking flash permissions..."

# 1. disk group (raw block device access)
if id -nG "$USER" | grep -qw disk; then
    echo "[ok] $USER in disk group"
else
    echo "[install] adding $USER to disk group"
    sudo usermod -aG disk "$USER"
    echo "    Relogin required for group change to take effect."
fi

# 2. Polkit rule for udisksctl mount
if pkcheck --action-id org.freedesktop.udisks2.filesystem-mount \
     --process $$ --allow-user-interaction >/dev/null 2>&1; then
    echo "[ok] polkit udisks mount rule"
else
    echo "[install] polkit udisks mount rule"
    sudo tee /etc/polkit-1/rules.d/50-udisks-noauth.rules > /dev/null << 'EOF'
polkit.addRule(function(action, subject) {
    if ((action.id == "org.freedesktop.udisks2.filesystem-mount" ||
         action.id == "org.freedesktop.udisks2.filesystem-mount-other-seat" ||
         action.id == "org.freedesktop.udisks2.filesystem-unmount-others") &&
        subject.isInGroup("wheel")) {
        return polkit.Result.YES;
    }
});
EOF
fi

# 3. udev rule for Tegra recovery USB
UDEV=/etc/udev/rules.d/99-tegra-flash.rules
if [ -f "$UDEV" ]; then
    echo "[ok] Tegra USB udev rule"
else
    echo "[install] Tegra USB udev rule"
    sudo tee "$UDEV" > /dev/null << 'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="0955", ATTR{idProduct}=="7e19", GROUP="disk", MODE="0660"
EOF
    sudo udevadm control --reload-rules
fi

echo ""
echo "Done. Flash with: cd /tmp/flash && ./initrd-flash --erase-nvme"
