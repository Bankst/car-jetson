#!/bin/bash
# P2P-GO test script — bypasses NetworkManager entirely.
# WARNING: Stops NetworkManager. WiFi client connection will drop.
# Reboot or `systemctl start NetworkManager` to recover.
set -euo pipefail

WIFI_IFACE="wlp192s0"
P2P_FREQ=5180          # Channel 36, 5GHz non-DFS
GO_IP="192.168.49.1"
DHCP_RANGE="192.168.49.2,192.168.49.254,255.255.255.0,24h"
WPA_CONF="/tmp/p2p-go-wpa.conf"
WPA_CTRL="/tmp/p2p-go-ctrl"

cleanup() {
    echo "[cleanup] tearing down..."
    killall dnsmasq 2>/dev/null || true
    killall wpa_supplicant 2>/dev/null || true
    ip link delete p2p-${WIFI_IFACE}-0 2>/dev/null || true
    rm -rf "$WPA_CTRL" "$WPA_CONF"
    echo "[cleanup] done. Run 'systemctl start NetworkManager' to restore WiFi."
}
trap cleanup EXIT

echo "=== P2P-GO Direct Test ==="
echo "This will stop NetworkManager and take direct control of $WIFI_IFACE."
echo ""

# 1. Stop NM so it doesn't interfere
echo "[1/7] Stopping NetworkManager..."
systemctl stop NetworkManager
sleep 1

# 2. Kill any existing wpa_supplicant
echo "[2/7] Killing existing wpa_supplicant..."
killall wpa_supplicant 2>/dev/null || true
sleep 1

# 3. Create minimal wpa_supplicant config with P2P support
echo "[3/7] Creating wpa_supplicant config..."
mkdir -p "$WPA_CTRL"
cat > "$WPA_CONF" <<EOF
ctrl_interface=$WPA_CTRL
ctrl_interface_group=wheel
update_config=1

device_name=BanksAA
device_type=1-0050F204-1
p2p_go_ht40=1

# Keep the managed interface connected (optional — skip for pure P2P test)
EOF

# 4. Start wpa_supplicant with P2P support
echo "[4/7] Starting wpa_supplicant..."
wpa_supplicant -B -i "$WIFI_IFACE" -c "$WPA_CONF" -C "$WPA_CTRL" -D nl80211
sleep 2

# Verify control socket
if [ ! -S "$WPA_CTRL/$WIFI_IFACE" ]; then
    echo "FAIL: wpa_supplicant control socket not created"
    exit 1
fi
echo "    wpa_supplicant running, ctrl=$WPA_CTRL/$WIFI_IFACE"

# 5. Create autonomous P2P Group Owner
echo "[5/7] Creating P2P-GO group on freq=$P2P_FREQ..."
wpa_cli -p "$WPA_CTRL" -i "$WIFI_IFACE" p2p_group_add freq=$P2P_FREQ
sleep 3

# Find the P2P-GO interface
GO_IFACE=$(ip -o link show | grep "p2p-" | awk -F': ' '{print $2}' | head -1)
if [ -z "$GO_IFACE" ]; then
    echo "FAIL: no P2P-GO interface created"
    wpa_cli -p "$WPA_CTRL" -i "$WIFI_IFACE" status
    exit 1
fi
echo "    GO interface: $GO_IFACE"

# Show GO details
echo ""
echo "=== GO Interface Details ==="
iw dev "$GO_IFACE" info
echo ""

# Get SSID and passphrase from wpa_cli
echo "=== GO Credentials ==="
wpa_cli -p "$WPA_CTRL" -i "$GO_IFACE" status | grep -E "ssid|p2p_device|freq|mode|key_mgmt"
echo ""
echo "Passphrase:"
wpa_cli -p "$WPA_CTRL" -i "$GO_IFACE" p2p_get_passphrase
echo ""

# 6. Assign IP and start DHCP
echo "[6/7] Assigning $GO_IP to $GO_IFACE and starting DHCP..."
ip addr add "$GO_IP/24" dev "$GO_IFACE"
dnsmasq --interface="$GO_IFACE" --dhcp-range="$DHCP_RANGE" \
        --bind-interfaces --no-resolv --no-hosts --log-dhcp &
sleep 1

# 7. Ready
echo ""
echo "=== P2P-GO ACTIVE ==="
echo "Interface:  $GO_IFACE"
echo "IP:         $GO_IP"
echo "DHCP:       $DHCP_RANGE"
echo ""
echo "On your phone:"
echo "  Settings → WiFi → WiFi Direct"
echo "  Look for 'DIRECT-xx-BanksAA'"
echo ""
echo "To test TCP after phone connects:"
echo "  nc -l $GO_IP 5000"
echo ""
echo "Press Enter to tear down and exit..."
read -r
