#!/bin/bash
# Install config files for WiFi Direct P2P-GO alongside NetworkManager.
# NM keeps managing wlan; p2p-* interfaces stay unmanaged.
# Run once as root on the target.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run as root" >&2
    exit 1
fi

NM_CONF="/etc/NetworkManager/conf.d/99-p2p-unmanaged.conf"
DBUS_CONF="/etc/dbus-1/system.d/wpa_supplicant-p2p.conf"

# --- 1. NM: leave p2p-* interfaces alone ---
NM_CONTENT='[keyfile]
unmanaged-devices=interface-name:p2p-*'

if [ -f "$NM_CONF" ] && grep -qF 'unmanaged-devices=interface-name:p2p-*' "$NM_CONF" 2>/dev/null; then
    echo "[skip] $NM_CONF already installed"
else
    mkdir -p "$(dirname "$NM_CONF")"
    printf '%s\n' "$NM_CONTENT" > "$NM_CONF"
    echo "[installed] $NM_CONF"
fi

# --- 2. D-Bus: allow wheel group to talk to wpa_supplicant ---
DBUS_CONTENT='<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy group="wheel">
    <allow send_destination="fi.w1.wpa_supplicant1"/>
    <allow receive_sender="fi.w1.wpa_supplicant1"/>
  </policy>
</busconfig>'

if [ -f "$DBUS_CONF" ] && grep -qF 'fi.w1.wpa_supplicant1' "$DBUS_CONF" 2>/dev/null; then
    echo "[skip] $DBUS_CONF already installed"
else
    mkdir -p "$(dirname "$DBUS_CONF")"
    printf '%s\n' "$DBUS_CONTENT" > "$DBUS_CONF"
    echo "[installed] $DBUS_CONF"
fi

# --- 3. Reload services ---
echo "[reload] NetworkManager..."
systemctl reload NetworkManager 2>/dev/null || systemctl restart NetworkManager
echo "[reload] dbus..."
systemctl reload dbus 2>/dev/null || true

# --- Done ---
echo ""
echo "Setup complete. p2p-* interfaces are now unmanaged by NM."
echo "wheel group can call wpa_supplicant D-Bus methods."
echo ""
echo "Next: run banks-frontend/tools/p2p-go-nm-test.sh (no sudo needed for P2P)."
