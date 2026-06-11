# Bluetooth A2DP Sink

Full BT audio sink stack baked into meta-seeed-jetson (commit 9beb609). Working end-to-end: phone pairs, streams A2DP, PipeWire routes to USB headset (Sennheiser), AVRCP metadata + playback control via media player TUI.

## Recipes
- `recipes-connectivity/bt-audio-agent/` — Python3 D-Bus pairing agent
  - KeyboardDisplay capability → Numeric Comparison → auto-accepts RequestConfirmation
  - Fixed PIN from `/etc/bluetooth/pin` (default 1234)
  - GLib mainloop via ctypes (avoids python3-pygobject/cairo dep chain)
  - `g_main_loop_new.restype = c_void_p` — critical on aarch64 (64-bit ptr)
- `recipes-connectivity/libfreeaptx/libfreeaptx_0.2.2.bb` — aptX/aptX-HD (regularhunter fork, LGPL)
- `recipes-connectivity/libldac/libldac_git.bb` — LDAC 990kbps (EHfive/ldacBT, gitsm://)
- `recipes-multimedia/pipewire/pipewire_%.bbappend` — `bluez-aac bluez-aptx bluez-ldac` PACKAGECONFIG
- `recipes-multimedia/wireplumber/wireplumber_%.bbappend` — all headless fixes (see below)

## WirePlumber headless fixes
WirePlumber is designed for user sessions (logind seat). On headless system service, four things break:

1. `DBUS_SESSION_BUS_ADDRESS` unset → `dbus-connection` module fails to load → cascading skips
   - Fix: service drop-in `Environment=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket`
2. `monitor.bluez.seat-monitoring` loads → sees `seat=offline` → BT monitor DEACTIVATED
   - Fix: `10-headless-bt.conf`: `monitor.bluez.seat-monitoring = disabled`
3. pipewire user (home=`/`) can't create `/.local/state/wireplumber`
   - Fix: `tmpfiles.d/wireplumber-state.conf` creates `/.local/state/wireplumber` owned by pipewire
4. D-Bus `ReserveDevice1` crashes WirePlumber → **no ALSA devices enumerated at all**. The reserve-device module tries to call `RequestRelease` on `org.freedesktop.ReserveDevice1.AudioN` which has no owner on the system bus → crash → restart loop. ALSA monitor activates, udev enumerates cards, but WirePlumber dies before creating PipeWire nodes.
   - Fix: `30-disable-reserve.conf`: `monitor.alsa.reserve-device = disabled`
   - Also: null sink must use `adapter` factory, not `spa-node-factory` — the latter doesn't advertise formats WirePlumber can negotiate, causing `no usable format found` errors.

## HFP cycling fix
Workstation Handsfree profile triggers HFP connection → WirePlumber creates `bluez_input.<mac>.N` → can't find ALSA sink → 5s timeout → destroy → repeat = rescan storm.
- Fix: `20-bt-a2dp-only.conf`: `bluez5.profiles = [a2dp-sink]`, `autoswitch-to-headset-profile = false`
- Fix: `pipewire.conf.d/10-null-sink.conf`: null-audio-sink fallback

## Codec status
| Codec | Status |
|-------|--------|
| SBC / SBC-XQ | ✓ built-in |
| AAC | ✓ fdk-aac via bluez-aac PACKAGECONFIG |
| Opus | ✓ built-in |
| aptX / aptX-HD | ✓ libfreeaptx recipe; pipewire shows "not available" until full image rebuild |
| LDAC | ✓ libldac recipe; same |

QCA BT firmware: `linux-firmware-qca` in packagegroup (rampatch_usb_00000302.bin).

Pairing: modern devices use SSP → KeyboardDisplay → Numeric Comparison → agent auto-accepts. KeyboardOnly triggers Passkey Entry → agent returns wrong fixed value → pairing fails.
