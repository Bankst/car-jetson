#!/bin/sh
# Banks Jetson Linux — USB device-mode gadget bring-up
#
# Creates an L4T-style USB composite gadget via configfs:
#   - NCM (CDC ethernet) on usb0  →  bridged into l4tbr0 by NetworkManager
#   - ACM (CDC serial)  on /dev/ttyGS0
#
# NetworkManager handles all IP/DHCP via its "shared" mode on l4tbr0.
set -eu

GADGET=/sys/kernel/config/usb_gadget/l4t

# Refuse to double-register
[ -d "$GADGET" ] && exit 0

modprobe libcomposite || true
mkdir -p "$GADGET"
cd "$GADGET"

echo 0x0955 > idVendor      # NVIDIA Corp
echo 0x7020 > idProduct     # L4T device mode
echo 0x0100 > bcdDevice
echo 0x0200 > bcdUSB
echo 0xEF   > bDeviceClass
echo 0x02   > bDeviceSubClass
echo 0x01   > bDeviceProtocol

mkdir -p strings/0x409
echo "$(cat /etc/machine-id 2>/dev/null || echo banks-jetson)" > strings/0x409/serialnumber
echo "Banks Jetson Linux"     > strings/0x409/manufacturer
echo "L4T USB Device Mode"    > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "L4T USB Device Mode" > configs/c.1/strings/0x409/configuration
echo 250                   > configs/c.1/MaxPower

mkdir -p functions/ncm.usb0
mkdir -p functions/acm.gs0

ln -s functions/ncm.usb0 configs/c.1/
ln -s functions/acm.gs0  configs/c.1/

# Bind to the first available USB Device Controller (Tegra: 3550000.xudc).
UDC=$(ls /sys/class/udc/ | head -n 1)
[ -n "$UDC" ] || { echo "no UDC found"; exit 1; }
echo "$UDC" > UDC
