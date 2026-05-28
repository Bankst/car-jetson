#!/bin/bash
# P2P-GO test — coexists with NetworkManager.
# Prereq: run scripts/setup-wifi-direct.sh once (as root).
set -euo pipefail

WIFI_IFACE="${WIFI_IFACE:-wlan0}"
P2P_FREQ="${P2P_FREQ:-5180}"
GO_IP="192.168.49.1"
DHCP_RANGE="192.168.49.2,192.168.49.254,255.255.255.0,24h"
GO_IFACE=""

# All wpa_supplicant interaction via this python helper
wpa_dbus() {
    python3 -c "
import dbus, sys, time, subprocess

bus = dbus.SystemBus()
supplicant = bus.get_object('fi.w1.wpa_supplicant1', '/fi/w1/wpa_supplicant1')
iface_mgr = dbus.Interface(supplicant, 'fi.w1.wpa_supplicant1')
sprops = dbus.Interface(supplicant, 'org.freedesktop.DBus.Properties')

cmd = '$1'

if cmd == 'remove_group':
    # Properly remove any existing P2P group via wpa_supplicant
    ifaces = sprops.Get('fi.w1.wpa_supplicant1', 'Interfaces')
    for ipath in ifaces:
        iobj = bus.get_object('fi.w1.wpa_supplicant1', ipath)
        ip = dbus.Interface(iobj, 'org.freedesktop.DBus.Properties')
        name = str(ip.Get('fi.w1.wpa_supplicant1.Interface', 'Ifname'))
        if name.startswith('p2p-'):
            print(f'Removing P2P group on {name}')
            try:
                p2p = dbus.Interface(iobj, 'fi.w1.wpa_supplicant1.Interface.P2PDevice')
                p2p.Disconnect()
            except Exception as e:
                print(f'  Disconnect failed: {e}')
            # Also try RemoveInterface
            try:
                iface_mgr.RemoveInterface(dbus.ObjectPath(ipath))
            except Exception as e:
                print(f'  RemoveInterface: {e}')
            time.sleep(1)

elif cmd == 'create_group':
    try:
        iface_path = iface_mgr.GetInterface('$WIFI_IFACE')
    except dbus.exceptions.DBusException:
        print('ERROR: interface $WIFI_IFACE not found', file=sys.stderr)
        sys.exit(1)

    iobj = bus.get_object('fi.w1.wpa_supplicant1', iface_path)
    p2p = dbus.Interface(iobj, 'fi.w1.wpa_supplicant1.Interface.P2PDevice')

    params = dbus.Dictionary({
        'frequency': dbus.Int32($P2P_FREQ),
        'persistent': dbus.Boolean(False),
    }, signature='sv')

    p2p.GroupAdd(params)

    # Wait for p2p interface
    for i in range(50):
        time.sleep(0.1)
        result = subprocess.run(['ip', '-o', 'link', 'show'], capture_output=True, text=True)
        for line in result.stdout.splitlines():
            parts = line.split(': ')
            if len(parts) >= 2 and parts[1].startswith('p2p-'):
                print(parts[1].split('@')[0])
                sys.exit(0)

    print('ERROR: p2p interface did not appear', file=sys.stderr)
    sys.exit(1)

elif cmd == 'credentials':
    ifaces = sprops.Get('fi.w1.wpa_supplicant1', 'Interfaces')
    for ipath in ifaces:
        iobj = bus.get_object('fi.w1.wpa_supplicant1', ipath)
        ip = dbus.Interface(iobj, 'org.freedesktop.DBus.Properties')
        name = str(ip.Get('fi.w1.wpa_supplicant1.Interface', 'Ifname'))
        if name == '$WIFI_IFACE':
            try:
                group_path = str(ip.Get('fi.w1.wpa_supplicant1.Interface.P2PDevice', 'Group'))
                if group_path and group_path != '/':
                    gobj = bus.get_object('fi.w1.wpa_supplicant1', group_path)
                    gp = dbus.Interface(gobj, 'org.freedesktop.DBus.Properties')
                    ssid = ''.join(chr(b) for b in gp.Get('fi.w1.wpa_supplicant1.Group', 'SSID'))
                    psk = str(gp.Get('fi.w1.wpa_supplicant1.Group', 'Passphrase'))
                    print(f'SSID={ssid}')
                    print(f'PSK={psk}')
            except Exception as e:
                print(f'ERR={e}')
" 2>&1
}

cleanup() {
    echo ""
    echo "[cleanup] removing P2P group via wpa_supplicant..."
    wpa_dbus remove_group || true
    if [ -n "$GO_IFACE" ]; then
        sudo ip addr del "$GO_IP/24" dev "$GO_IFACE" 2>/dev/null || true
    fi
    sudo pkill -f "dnsmasq.*p2p" 2>/dev/null || true
    echo "[cleanup] done."
}
trap cleanup EXIT

echo "=== P2P-GO Test (NM coexistence) ==="
echo "WiFi interface: $WIFI_IFACE"
echo "P2P frequency:  $P2P_FREQ MHz"
echo ""

# Preflight
systemctl is-active --quiet NetworkManager || { echo "ERROR: NM not running"; exit 1; }
python3 -c "import dbus" 2>/dev/null || { echo "ERROR: python3-dbus missing"; exit 1; }
busctl status fi.w1.wpa_supplicant1 >/dev/null 2>&1 || { echo "ERROR: wpa_supplicant not on D-Bus"; exit 1; }

# 0. Remove any existing P2P group properly
echo "[0/4] Cleaning existing P2P groups via D-Bus..."
wpa_dbus remove_group
sleep 2

# 1. Create P2P-GO
echo "[1/4] Creating P2P-GO group..."
GO_IFACE=$(wpa_dbus create_group)
if [ -z "$GO_IFACE" ]; then
    echo "FAIL: no GO interface" >&2
    exit 1
fi
echo "    GO interface: $GO_IFACE"

# 2. Credentials
echo ""
echo "[2/4] GO credentials..."
wpa_dbus credentials
echo ""
echo "    iw info:"
iw dev "$GO_IFACE" info 2>/dev/null | grep -E "ssid|channel|type|txpower" || true

# 3. IP + DHCP
echo ""
echo "[3/4] Assigning $GO_IP and starting dnsmasq..."
sudo pkill -f "dnsmasq.*p2p" 2>/dev/null || true
sleep 0.5
sudo ip addr add "$GO_IP/24" dev "$GO_IFACE" 2>/dev/null || true
sudo ip link set "$GO_IFACE" up 2>/dev/null || true
sudo dnsmasq --interface="$GO_IFACE" --dhcp-range="$DHCP_RANGE" \
    --bind-interfaces --no-resolv --no-hosts --log-dhcp &
sleep 1

# 4. Ready
echo ""
echo "=== P2P-GO ACTIVE ==="
echo "Interface:  $GO_IFACE"
echo "IP:         $GO_IP"
echo "NM:         $(nmcli -t -f STATE general 2>/dev/null || echo '?')"
echo ""
echo "On phone: WiFi Direct → look for DIRECT-xx"
echo ""
echo ">>> P2P-GO running. Waiting 60s for you to check phone. <<<"
echo ">>> Press Ctrl-C or wait to tear down. <<<"
sleep 60
