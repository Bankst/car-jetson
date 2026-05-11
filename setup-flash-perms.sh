#!/bin/bash
set -e

# Tegra RCM recovery device
sudo tee /etc/udev/rules.d/99-tegra-flash.rules > /dev/null << 'EOF'
SUBSYSTEM=="usb", ATTR{idVendor}=="0955", ATTR{idProduct}=="7e19", MODE="0660", GROUP="disk"
SUBSYSTEM=="usb", ATTR{idVendor}=="1d6b", ATTR{idProduct}=="0104", MODE="0660", GROUP="disk"
SUBSYSTEM=="block", ATTRS{idVendor}=="1d6b", ATTRS{idProduct}=="0104", MODE="0660", GROUP="disk"
EOF

# Polkit rule for udisks2 block device access
sudo tee /etc/polkit-1/rules.d/99-tegra-flash.rules > /dev/null << 'EOF'
polkit.addRule(function(action, subject) {
    if (action.id == "org.freedesktop.udisks2.open-device" && subject.isInGroup("disk")) {
        return polkit.Result.YES;
    }
});
EOF

sudo usermod -aG disk "$USER"
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "Done. Log out and back in for group change to take effect."
