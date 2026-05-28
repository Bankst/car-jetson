# Banks Jetson Linux

Yocto OE4T image targeting **Jetson Xavier NX production module on a SeeedStudio A203 V2 carrier**.
Built and flash-tested. Boot from eMMC, rootfs on external NVMe (M.2 Key M 2242).

## Stack snapshot

| Component | Choice | Reasoning |
|---|---|---|
| Yocto release | **scarthgap** (5.0 LTS) | Required by meta-tegra L4T R35.6.x branch — meta-tegra has no kirkstone-l4t-r35 |
| meta-tegra branch | **`scarthgap-l4t-r35.x`** | L4T R35.6.4 / JetPack 5.1.6, the latest L4T that still supports Xavier NX (dropped in JP6/R36) |
| Kernel | linux-tegra 5.10.216 (NVIDIA's fork) | comes with the BSP |
| Build framework | **kas-container** via Docker | Fedora 43 host isn't Yocto-supported; container sidesteps host-deps mess |
| MACHINE | `jetson-xavier-nx-a203` (production) / `jetson-xavier-nx-banks-devkit` (dev) | Both `require jetson-xavier-nx-devkit-emmc.conf` and override storage + bootloader. Devkit variant exposes the 40-pin header (DAP5/I2S5 for PCM5102A, full UART debug). |
| DISTRO | `banks-jetson` | Banks's flavour, defined in `meta-seeed-jetson/conf/distro/banks-jetson.conf` |
| Init | systemd | |
| Networking | NetworkManager (default) + systemd-networkd (kept available for nothing in particular; networkd is currently masked from owning anything) | NM owns Ethernet/Wi-Fi/USB-gadget bridge; CAN0 brought up at 500kbps via NM keyfile (NM 1.46+ has [can] section) |
| DE | Weston 10 kiosk (`kas/kiosk.yml`) or KDE Plasma 6 (`kas/plasma.yml`) | Decoupled — base image is DE-agnostic. Kiosk: built+flashed, working. Plasma: not yet built. |
| Bootloader | **`tegra-uefi-prebuilt`** (NVIDIA-shipped UEFI binary) | source-build EDK2 (`edk2-firmware-tegra`) fails on scarthgap GCC 13 with GenFw PE-COFF errors; prebuilt avoids the issue and is what NVIDIA officially ships |
| Storage | Boot/UEFI on QSPI/eMMC, rootfs on `/dev/nvme0n1p1` | `TNSPEC_BOOTDEV` in MACHINE conf, `EXTERNAL_ROOTFS_DRIVE=1` |

## Layer set

| Layer | Branch / Tag | Notes |
|---|---|---|
| poky | scarthgap | |
| meta-openembedded (oe, python, networking, multimedia, filesystems) | scarthgap | |
| meta-tegra | `scarthgap-l4t-r35.x` | (note the `.x` suffix) |
| meta-qt6 | tag `v6.9.0` | Plasma target only |
| yocto-meta-kf6 | `master` | Plasma target; LAYERSERIES_COMPAT override needed (see below) |
| yocto-meta-kde | `master` | Plasma target; ditto |
| meta-seeed-jetson | local | Our layer — see structure below |

## Repo / layer structure

```
scripts/
  setup-host-flash-permissions.sh   # polkit rule for unattended initrd-flash (udisksctl mount)

kas/
  base.yml          # poky + meta-oe + meta-tegra + machine; DE-agnostic
  kiosk.yml         # includes base.yml; adds Xorg + matchbox-wm + kiosk session; selects kiosk image
  plasma.yml        # includes base.yml; adds Qt6 + KF6 + KDE; selects plasma image
  lxqt.yml          # placeholder, future LXQt variant

meta-seeed-jetson/
  conf/
    layer.conf                            # also injects LAYERSERIES_COMPAT scarthgap into Qt6/KF6/KDE — must be in a layer.conf, NOT local.conf, since the compat check runs before local.conf parses
    distro/banks-jetson.conf
    machine/jetson-xavier-nx-a203.conf
  classes/
    kf6-{cmake,ki18n,kcoreaddons,kconfig,kauth,kcmutils,kdoctools}.bbclass
                                          # one-line shims around kf6_* (underscore) — yocto-meta-kde master inherits hyphenated names that yocto-meta-kf6 master ships under underscored filenames; this is upstream drift, not our bug
  recipes-bsp/
    tegra-bsp-a203/                       # ships A203 DTBs + pinmux from Seeed's JP 5.1.4 driver pack via deploy class
      files/                              # 3 DTBs + 1 pinmux .cfg, byte-for-byte from _input/203_jp514.tar.gz
    tegra-bootfiles/                      # pinmux substitution bbappend on meta-tegra's tegra-bootfiles
    banks-jetson-iface/                   # carrier-agnostic NX interface bring-up; included via MACHINE_EXTRA_RDEPENDS by both A203 and devkit machine confs
      banks-usb-gadget.sh                 # configfs-based USB gadget setup: NCM ethernet on usb0, ACM serial on ttyGS0
      banks-usb-gadget.service            # systemd, runs at boot before NetworkManager
      l4tbr0.nmconnection                 # NM keyfile: bridge with method=shared (auto DHCP server on 192.168.55.1/24)
      l4t-gadget-usb0.nmconnection        # NM keyfile: usb0 as bridge slave
      can0.nmconnection                   # CAN0 at 500kbps
      banks-jetson-modules.conf           # /etc/modules-load.d/ — mttcan, can*, spidev
  recipes-graphics/
    banks-kiosk/                          # kiosk session: weston + media player TUI + shell toggle (triggerhappy)
      files/banks-media-player            # BT AVRCP media controller (Python curses TUI)
      files/kiosk-launcher                # weston startup with kiosk/desktop shell toggle
      files/toggle-desktop-mode           # Ctrl+Shift+D handler (flock debounce)
      files/weston-kiosk.ini              # idle-time=0, require-input=false
  recipes-kernel/linux/
    linux-tegra_%.bbappend                # injects 4 config fragments + DTB substitution at do_deploy
    linux-tegra/can.cfg                   # CAN_*, MTTCAN
    linux-tegra/spi.cfg                   # SPIDEV
    linux-tegra/usb-modem.cfg             # CDC_NCM, USB_SERIAL_OPTION, USB_WDM (cellular USB modems, off by default)
    linux-tegra/usb-gadget.cfg            # USB_GADGET, CONFIGFS_*, NCM/ACM/RNDIS/ECM
    linux-tegra/audio-soc.cfg             # devkit only: Tegra ASoC + I2S5 + AHUB + ADMAIF + ADSP + SPDIF for PCM5102A
    linux-tegra/no-audio-soc.cfg          # A203 only: disable SoC audio (no I2S codec on carrier)
  recipes-core/
    banks-persist/                        # UDA partition persistence: format, mount /data, bind-mount BT keys + SSH keys
    images/banks-jetson-image-{base,kiosk,plasma,lxqt}.bb
    packagegroups/{packagegroup-seeed-base, packagegroup-kiosk, packagegroup-de-plasma-minimal}.bb

_input/
  203_jp514.tar.gz                        # Seeed's A203 driver pack for JP 5.1.4 (Xavier NX)
  extracted/                              # untracked; staging for the few files we actually use
  *_WRONG-BOARD-Orin-A603                 # the Orin tarball we got first by mistake; renamed for safety
```

## Image targets

| kas yaml | Image recipe | Status |
|---|---|---|
| `kas/base.yml` | `banks-jetson-image-base` | ✓ Built, flashed, daily-driver |
| `kas/kiosk.yml` | `banks-jetson-image-kiosk` | ✓ Built, flashed, working |
| `kas/plasma.yml` | `banks-jetson-image-plasma` | Not yet built |
| `kas/lxqt.yml` | `banks-jetson-image-lxqt` | Placeholder |

### Kiosk image — display stack notes

L4T R35 (JP5) dropped the X11 DDX driver. `/dev/dri/card0` is `tegra_udrm` — NVIDIA's GPU compute device with no KMS connectors. Xorg `xf86-video-modesetting` finds it but fails with "no screens found". **Wayland via EGL/GBM is the only display path.**

- **Weston version**: meta-tegra forces `PREFERRED_VERSION_weston = "10.0%"` in `tegra-common.inc` → meta-tegra's 10.0.2 wins over poky's 13.
- **Display wiring**: `meta-tegra/conf/layer.conf` remaps `libdrm → libdrm-nvdc → tegra-libraries`. Weston's DRM backend talks to NVIDIA NVDC automatically.
- **Launcher**: `kiosk.service` (`Restart=always`) runs `/etc/kiosk/kiosk-launcher`, which starts weston (`XDG_RUNTIME_DIR=/run/kiosk`, socket `wayland-kiosk`), waits for socket, then execs `/etc/kiosk/kiosk-app`.
- **Shell toggle**: `kiosk-launcher` checks `/var/lib/kiosk/desktop-mode` flag: absent = `kiosk-shell.so` (no panel, fullscreen), present = `desktop-shell.so` (top bar, `Super+Tab` for window switching). Toggled via `Ctrl+Shift+D` (triggerhappy daemon, `kiosk-keys.conf`). Toggle script uses flock debounce (NanoKVM sends duplicate key events from two kbd devices).
- **Default kiosk app**: `banks-media-player` — Python curses TUI showing BT AVRCP metadata (title/artist/album/progress) with play/pause/next/prev/volume controls. Keys: `space` play/pause, `n`/right next, `p`/left prev, `up`/`+` vol up, `down`/`-` vol down, `m` mute. Uses `dbus-monitor` thread for near-instant track updates + `dbus-send` for commands + `wpctl` for volume.
- **Desktop mode app**: `weston-terminal --maximized` with bash login shell (sources `/etc/profile`).
- **Idle/lock disabled**: `weston-kiosk.ini` sets `idle-time=0`. Without this, weston's screen locker activates after 5 min.
- **weston-init masked**: kiosk image masks `weston.service` + `weston.socket` from `weston-init` recipe to prevent compositor conflict on tty7.
- **Mouse/touch**: weston-terminal 10 does NOT support mouse reporting (no DECSET 1000/1006). Touch-enabled TUI would need `foot` terminal (not in any available OE layer) or a native Wayland app.
- **PACKAGE_ARCH**: `packagegroup-kiosk` must set `PACKAGE_ARCH = "${MACHINE_ARCH}"`. Allarch packagegroups can't depend on dynamically-renamed NVIDIA packages (e.g. `egl-gbm → libnvidia-egl-gbm`).
- **GPU check**: `WAYLAND_DISPLAY=wayland-kiosk XDG_RUNTIME_DIR=/run/kiosk weston-info` or `glmark2-wayland` (deb available, push with `jtx push glmark2`).

### efi-timeout — UEFI boot countdown

`meta-seeed-jetson/recipes-bsp/efi-timeout/` — first-boot oneshot service. Writes `Timeout` EFI variable (UINT16=0) to QSPI via efivarfs, eliminating the L4TLauncher countdown. Stamps `/var/lib/efi-timeout-configured` so it only runs once. In `packagegroup-seeed-base` — all images get it. Push to live board: `jtx push efi-timeout`.

## Build & flash

### One-time host setup

```sh
# Fedora 43 packages required for the flash step (NOT for the build — kas-container is hermetic)
sudo dnf install -y dtc vim-common gdisk bmap-tools cpp lz4

# Docker group access (after install + relogin to load group)
sudo usermod -aG docker "$USER"
# Either log out/back in fully (Plasma session inherits groups at login),
# OR wrap each command in: sg docker -c '...'

# Unattended flash: allow udisksctl mount without password (initrd-flash uses udisksctl)
./scripts/setup-host-flash-permissions.sh
```

### Build

```sh
# Always set both env vars; SELinux escape is mandatory on Fedora enforcing.
KAS_BUILD_DIR=$PWD/build KAS_RUNTIME_ARGS="--security-opt label=disable" \
  kas-container build kas/base.yml          # ~30–60 min cold; minutes warm

# Plasma variant (also rebuild base sstate)
KAS_BUILD_DIR=$PWD/build KAS_RUNTIME_ARGS="--security-opt label=disable" \
  kas-container build kas/plasma.yml        # +1–2 hours cold for KDE
```

### Build with distributed icecc (optional, faster)

Adds remote node `dev-ct` (EPYC 7302P, 32t, Tailscale 100.74.250.91) as
compile farm. Scheduler runs on this Ryzen (100.72.134.17). Aggregate 48t
(16 local + 32 remote). dev-ct is on Tailscale directly — icecc uses
Tailscale IPs throughout.

Prereqs (one-time):
- `icecc-scheduler` running on Ryzen (`systemctl start icecc-scheduler`).
  Listens on port 8765.
- `iceccd` on Ryzen: `ICECC_SCHEDULER_HOST=100.72.134.17`, 16 jobs,
  `ICECC_NETNAME=banks-yocto`. Config: `/etc/icecc/icecc.conf`.
- `iceccd` on dev-ct (100.74.250.91): `ICECC_SCHEDULER_HOST=100.72.134.17`,
  32 jobs, same netname. Config: `/etc/icecc/icecc.conf`.
  No scheduler on dev-ct (`systemctl stop icecc-scheduler` there).
- Verify both registered: `ss -tn 'dport = :8765'` should show two ESTAB
  connections to 100.72.134.17:8765.
- Custom kas image with the icecc client: `docker build -t kas-icecc:4.7
  -f docker/kas-icecc.Dockerfile docker/`.

Invoke:

```sh
KAS_BUILD_DIR=$PWD/build \
KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
  kas-container \
    --runtime-args "--network=host" \
    --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
    build kas/base.yml
```

Two non-obvious wiring details:

1. **`KAS_RUNTIME_ARGS` env var is overwritten** by `kas-container` script
   (line 232) — pass via `--runtime-args` CLI flag instead.
2. **The local iceccd Unix socket must be bind-mounted** into the container
   at `/var/run/icecc/iceccd.socket`. Without this the icecc client falls
   back to building locally (silent regression). The mount is the
   `-v /run/icecc:/var/run/icecc:rw` line.

`--network=host` is required so the local iceccd (talking via the socket)
sees the scheduler on the LAN. Monitor placement with `icemon` on dev-ct, or
`ss -tn '( sport = :10245 )'` on 100.74.250.91 to count live jobs.
See `docker/README.md` for full notes.

**One-time sstate cost**: adding `INHERIT += "icecc"` changes recipe task
signatures across the board, so the first build after enabling will rebuild
most things (≈10 min for `bitbake openssl` + cascade in testing). After
that, the new "icecc-aware" sstate is durable and subsequent builds hit
cache normally.

### Flash

```sh
# Module in recovery: short FC REC pin to GND, power cycle. lsusb should show 0955:7e19.

mkdir -p /tmp/flash
tar xzf build/tmp/deploy/images/jetson-xavier-nx-a203/banks-jetson-image-base-jetson-xavier-nx-a203.rootfs.tegraflash.tar.gz \
    -C /tmp/flash
cd /tmp/flash
sudo ./initrd-flash --erase-nvme            # ~5–15 min, then module reboots automatically
```

After reboot: USB-A203 micro-USB to host PC creates a CDC-NCM ethernet (host gets a `192.168.55.x` IP via the Jetson's NM `shared` DHCP) and a CDC-ACM serial. SSH `ssh root@192.168.55.1` (passwordless via `debug-tweaks` for now).

## Hardware notes

- No analog audio output on A203 V2 carrier — HDMI (card 0) and ADMAIF I2S (card 1, 20x XBAR-ADMAIF only)
- Serial console: ttyTHS0 at 115200, 40-pin header pins 8 (TX) / 10 (RX)
- SSH: `ssh root@192.168.55.1` — passwordless, authorized_keys installed
- SSH keys: `~/.ssh/id_rsa` (bankst@bankst-workstation) and `~/.ssh/id_ed25519_personal` (btrout.dhrs@gmail.com / GitHub) both in root + bankst authorized_keys

## Target tooling — jtx

Single-file script at `car-jetson/jtx`, symlinked to `~/.local/bin/jtx` (in PATH).

| Command | Notes |
|---|---|
| `jtx` / `jtx health` | Dashboard: thermals (≥60°C warn, ≥75°C err), load, mem, disk, uptime, network IPs, CAN0 bitrate/stats, SPI/serial devices, failed units |
| `jtx watch [N]` | Live-refresh health every N sec (default 3) |
| `jtx ping` | SSH reachability check |
| `jtx ssh [cmd]` | Interactive SSH or remote command |
| `jtx push <recipe\|deb> [svc]` | Find newest `.deb` under `build/tmp/deploy/deb/`, scp + dpkg-install; optionally restart service |
| `jtx logs [-f] [-b] [-n N] [unit]` | journald; follow, this-boot, N lines, unit filter |
| `jtx dmesg [-f] [-e]` | dmesg; `-f` follow, `-e` errors+warnings only |
| `jtx can [-f]` | CAN0 detail + nonzero stats; `-f` live refresh |
| `jtx spi` | SPI device listing |
| `jtx reboot` | Reboot (confirm prompt) |
| `jtx reboot-recovery` | `reboot forced-recovery` → device appears as USB 0955:7e19 for flashing |
| `jtx poweroff` | Power off (confirm prompt) |

Override: `JTX_TARGET=root@<ip>`, `JTX_DEPLOY_DEB=<path>`.

## Bluetooth A2DP Sink

Full BT audio sink stack baked into meta-seeed-jetson (commit 9beb609).

### Recipes
- `recipes-connectivity/bt-audio-agent/` — Python3 D-Bus pairing agent
  - KeyboardDisplay capability → Numeric Comparison → auto-accepts RequestConfirmation
  - Fixed PIN from `/etc/bluetooth/pin` (default 1234)
  - GLib mainloop via ctypes (avoids python3-pygobject/cairo dep chain)
  - `g_main_loop_new.restype = c_void_p` — critical on aarch64 (64-bit ptr)
- `recipes-connectivity/libfreeaptx/libfreeaptx_0.2.2.bb` — aptX/aptX-HD (regularhunter fork, LGPL)
- `recipes-connectivity/libldac/libldac_git.bb` — LDAC 990kbps (EHfive/ldacBT, gitsm://)
- `recipes-multimedia/pipewire/pipewire_%.bbappend` — `bluez-aac bluez-aptx bluez-ldac` PACKAGECONFIG
- `recipes-multimedia/wireplumber/wireplumber_%.bbappend` — all headless fixes (see below)

### WirePlumber headless fixes
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

### HFP cycling fix
Workstation Handsfree profile triggers HFP connection → WirePlumber creates `bluez_input.<mac>.N` → can't find ALSA sink → 5s timeout → destroy → repeat = rescan storm.
- Fix: `20-bt-a2dp-only.conf`: `bluez5.profiles = [a2dp-sink]`, `autoswitch-to-headset-profile = false`
- Fix: `pipewire.conf.d/10-null-sink.conf`: null-audio-sink fallback

### Codec status
| Codec | Status |
|-------|--------|
| SBC / SBC-XQ | ✓ built-in |
| AAC | ✓ fdk-aac via bluez-aac PACKAGECONFIG |
| Opus | ✓ built-in |
| aptX / aptX-HD | ✓ libfreeaptx recipe; pipewire shows "not available" until full image rebuild |
| LDAC | ✓ libldac recipe; same |

QCA BT firmware: `linux-firmware-qca` in packagegroup (rampatch_usb_00000302.bin).

Pairing: modern devices use SSP → KeyboardDisplay → Numeric Comparison → agent auto-accepts. KeyboardOnly triggers Passkey Entry → agent returns wrong fixed value → pairing fails.

## SD Card / DeviceTree

### How DTB reaches kernel
- UEFI (L4TLauncher / `bootaa64.efi`) does **not** load from `kernel-dtb` NVMe partition at runtime — MB2/firmware loads from QSPI, bypassing NVMe entirely.
- `kernel-dtb` and `kernel-dtb_b` NVMe partitions exist but are ignored by the running UEFI chain.
- **Fix**: `FDT` line in `/boot/extlinux/extlinux.conf` — L4TLauncher honors this and loads from rootfs.
- Baked via `UBOOT_EXTLINUX_FDT = "/boot/devicetree/tegra194-p3668-a203.dtb"` in `conf/machine/jetson-xavier-nx-a203.conf`.

### Custom DTB recipe
- `meta-seeed-jetson/recipes-bsp/seeed-a203-devicetree/` — `inherit devicetree`, compiles full DTB.
- Source: `tegra194-p3668-a203.dts` includes `tegra194-p3668-all-p3509-0000.dts` then `a203-sd.dtsi`.
- Installs to `/boot/devicetree/tegra194-p3668-a203.dtb` on rootfs.
- `PREFERRED_PROVIDER_virtual/dtb = "seeed-a203-devicetree"` in machine conf.

### SD card (sdhci@3440000 = mmc2)
- CD GPIO: PQ.02 (`&tegra_main_gpio 0x82`) — physically low when card inserted.
- Correct polarity: `cd-gpios = <&tegra_main_gpio 0x82 0x00>` (GPIO_ACTIVE_HIGH) + `cd-inverted;` from base DTB.
  - Base DTB already has `cd-inverted;` on sdhci@3440000 — this toggles gpiod active level = effective ACTIVE_LOW.
  - `GPIO_ACTIVE_LOW` (0x01) + `cd-inverted` = double inversion = **wrong** (don't do this).
- dmesg when working: `sdhci-tegra 3440000.sdhci: Got CD GPIO` then card enumerated as `mmc2` / `mmcblk2`.

### DTB deploy (no reflash needed)
```sh
# Build
kas-container (icecc) shell kas/base.yml -c "bitbake seeed-a203-devicetree"
# Push (reboot required — extlinux.conf read at boot by L4TLauncher)
jtx push seeed-a203-devicetree
```

### icecc required for kernel configure
`INHERIT += "icecc"` in `build/conf/local.conf` — kernel `do_configure` fails without icecc daemon running. Always use `kas-icecc:4.7` container with `--network=host` and `-v /run/icecc:/var/run/icecc:rw` for any build touching linux-tegra. Regular `kas-container shell` without icecc socket mount = kernel configure fails with "unknown assembler invoked".

### Force-rebuild a recipe bypassing sstate
```sh
kas-container shell kas/base.yml -c "bitbake -f -c do_install <recipe> && bitbake -f -c do_package <recipe> && bitbake -f -c do_package_write_deb <recipe>"
```

## Non-obvious workarounds in this repo (so future-you/me doesn't redo them)

1. **`KAS_RUNTIME_ARGS="--security-opt label=disable"`** is mandatory on Fedora enforcing. Without it the kas-container can't read `/repo` due to SELinux MCS labelling on bind mounts. Only mode that works is disabling label confinement for the container; `:Z` mount option fights kas-container's own `-v` lines.

2. **KDE/KF6 master branches drifted** — `yocto-meta-kde` does `inherit kf6-cmake` (hyphenated), `yocto-meta-kf6` ships `kf6_cmake.bbclass` (underscored). Our shims in `meta-seeed-jetson/classes/kf6-*.bbclass` bridge the gap. Remove if upstream syncs.

3. **`LAYERSERIES_COMPAT` override** for `qt6-layer`/`kf6`/`kde` lives in `meta-seeed-jetson/conf/layer.conf`, NOT in `local.conf`. The compat check runs after layer.confs but before local.conf is read. Their layer.confs claim "styhead walnascar" only, but KDE's own `yocto-manifest/scarthgap.xml` confirms master-on-scarthgap is the supported pairing.

4. **Bootloader: prebuilt UEFI** (`tegra-uefi-prebuilt`) instead of source-built EDK2. Source build of `edk2-firmware-tegra` on scarthgap GCC 13 fails with `GenFw: ERROR 3000: DOS header signature was not found in UiApp.dll` — PE-COFF conversion bug, likely `-flto` interaction. Prebuilt is what NVIDIA ships for L4T R35.6.4 and is well-validated.

5. **Don't `PACKAGECONFIG:remove "resolved"` from systemd** — it breaks `nss-resolve` which depends on it. NetworkManager and `systemd-resolved` coexist fine; resolved just provides DNS stub. We only remove `networkd` (and even that's currently *not* removed since we kept it available for the `l4t-usb-device-mode` recipe path, which we don't use anymore — the remove can be re-added if you want a leaner image).

6. **NetworkManager `nmtui` is gated by PACKAGECONFIG** — the `networkmanager-nmtui` package only exists if `nmtui` is in PACKAGECONFIG. We add it via `PACKAGECONFIG:append:pn-networkmanager = " nmtui"` in distro conf.

7. **A203 DTBs are byte-identical drop-ins** — Seeed kept NVIDIA's filenames (`tegra194-p3668-*.dtb`, `tegra19x-mb1-pinmux-p3668-a01.cfg`) so substitution is just "ship our copies; bbappend the kernel `do_deploy` to overwrite the kernel-built ones." Confirmed via md5: deployed DTB matches source 1:1.

8. **Cellular Quectel modem userspace + audio init script from the driver pack — DEFERRED.** Quectel is in `203_jp514.tar.gz` under `rootfs/leetop/quectel/`; A203 audio init is `code_spkmic.sh + startup.service`. Re-package both as recipes only if user actually needs them.

9. **USB gadget approach: NM owns the bridge.** We do *not* install meta-tegra's `l4t-usb-device-mode` recipe — it ships only systemd-networkd `.network`/`.netdev` files (and even then it's incomplete — the actual gadget creation script is missing from meta-tegra; NVIDIA ships it in their `nv-l4t-usb-device-mode` deb which meta-tegra doesn't pull in). Our setup: a small `banks-usb-gadget.sh` configfs script + systemd unit + NM keyfiles for the bridge, all under `recipes-bsp/banks-jetson-iface/` (carrier-agnostic; works on A203 and devkit P3509).

10. **GCC 13 vs L4T 5.10 kernel — three layers of fixes required.** L4T 5.10 was authored for GCC 11; scarthgap GCC 13 promotes four new warning classes to errors. Fixes:
    - **KCFLAGS** in `linux-tegra_%.bbappend`: `KCFLAGS='-Wno-address -Wno-implicit-fallthrough -Wno-int-in-bool-context -Wno-tautological-compare'` covers in-tree warnings from trace macros, ACPI, nvidia display, and the r8168 OOT module.
    - **Patch `0002-nvgpu-drop-gcc13-implicit-fallthrough-override.patch`**: nvgpu's `Makefile` explicitly adds `ccflags-y += $(call cc-option, -Wimplicit-fallthrough=3)` which appends *after* KCFLAGS in the compiler invocation, overriding the suppression. The patch removes that line.
    - **ATF bbappend** (`recipes-bsp/arm-trusted-firmware/arm-trusted-firmware_%.bbappend`): ATF's build system collects `-Werror` in its own `ERRORS` make variable (Makefile line 409), not KCFLAGS. `EXTRA_CFLAGS` has no effect. Fix: `EXTRA_OEMAKE:append = " ERRORS='-Werror -Wno-error=logical-op'"`.
    - These were exposed all at once because `INHERIT += "icecc"` changes all task hashes → full sstate miss → first icecc build rebuilt everything from scratch.

11. **Serial-tegra console patch: exists but NOT wired — causes initrd-flash lockup.** `linux-tegra/0001-serial-tegra-add-console-and-earlycon-support.patch` adds earlycon + late console to the tegra-hsuart driver. The patch is syntactically correct and compiled cleanly, but the initrd kernel (used by `initrd-flash`) locked up on flash. The patch is kept for future reference but is intentionally absent from `SRC_URI`. The `console=ttyTHS0,115200n8` and `earlycon=...` kernel args are also absent from `banks-jetson.conf`. Serial getty on `ttyTHS0` still works via the userspace `serial-getty@ttyTHS0.service` symlink in the image recipe — no kernel patch needed for that.

12. **Stale `bitbake.lock` / `bitbake.sock` block new builds after unclean container exit.** If `kas-container` is Ctrl-C'd or OOM-killed, these files remain in `build/`. The next `kas-container build` will hang waiting for the lock. Fix: `rm -f build/bitbake.lock build/bitbake.sock` before relaunching.

13. **`~/bstat` script for build status without prompting Claude.** Quick tail of the most recent `/tmp/r3-build-*.log` with error count. See the script at `~/bstat`.

14. **SSH login latency — root cause and fix.** Fresh SSH login was ~1.85s. Investigation (via strace bisection of PAM modules) revealed three compounding causes:
    - **pam_unix in PAM auth stack**: OpenSSH with `UsePAM yes` always runs `pam_authenticate()` in a dedicated pthread, communicating via a pipe IPC with the main sshd thread. pam_unix triggers this conversation mechanism even for pubkey/empty-password logins, adding ~0.6s on every connection regardless of auth method. Fix: **`UsePAM no`** — OpenSSH handles passwords natively via `/etc/shadow`; no PAM thread, no IPC.
    - **sntrup761 post-quantum KEX**: OpenSSH 9.x defaults to `sntrup761x25519-sha512` for key exchange. This hybrid post-quantum algorithm is expensive on ARM Carmel, adding ~170ms. Fix: `KexAlgorithms curve25519-sha256,ecdh-sha2-nistp256` in sshd_config.
    - **Socket-activated sshd**: `sshd.socket` + `sshd@.service` spawns a new sshd process per connection. Fix: pre-forked `sshd.service` (`sshd -D`); `sshd.socket` masked in image.
    - Result: **~0.20s** fresh pubkey login. Password auth works natively with `UsePAM no` + `PasswordAuthentication yes`.
    - `UsePrivilegeSeparation no` was also tested (~100ms saving) but the option was removed in OpenSSH 9.x — don't add it to sshd_config, sshd will refuse to start.
    - Logind session tracking lost with `UsePAM no` (no pam_systemd). XDG `/run/user/UID` covered by `loginctl enable-linger <user>`.
    - Baked in: `meta-seeed-jetson/recipes-connectivity/openssh/openssh_%.bbappend` ships custom `sshd_config` + `sshd.service`; image recipe masks `sshd.socket` and enables `sshd.service`.
    - **Strace artifacts**: measuring with `strace -e trace=all` caused `close_range()` to fail under ptrace → sshd fell back to 65535-iteration `close()` loop, making strace look like the bottleneck. Always filter strace traces (`-e trace=close,close_range` etc.) when measuring timing.

15. **`initrd-flash` prompts for password mid-flash.** `initrd-flash` uses `udisksctl mount` to access USB storage during the "create partitions" step. Polkit requires auth for `org.freedesktop.udisks2.filesystem-mount` without an active desktop session. Fix: `scripts/setup-host-flash-permissions.sh` installs a polkit rule allowing `wheel` group to mount without password. Run once on the build host.

16. **weston-terminal 10 has no mouse/touch reporting.** Curses `mousemask()` works but weston-terminal never sends mouse escape sequences to the pty. Fix: use `matchbox-terminal` (GTK3 + VTE, Wayland-native) which has proper mouse reporting. `foot` terminal not in any OE layer.

17. **Persistent data across reflash — UDA partition.** NVMe partition 15 (`PARTLABEL=UDA`, 400MB) is part of NVIDIA's standard flash layout and is unused by default. `banks-persist` recipe formats it as ext4 on first boot, mounts at `/data`, and bind-mounts `/var/lib/bluetooth` + SSH host keys from it. BT pairing keys and SSH host keys survive rootfs reflash (when flashing without `--erase-nvme`). First flash with `--erase-nvme` creates UDA empty; first boot initializes it; subsequent flashes without `--erase-nvme` preserve it. Recipe in `recipes-core/banks-persist/`, added to `packagegroup-seeed-base` so all images get it.
    - **UDA mount**: fstab entry with `nofail,x-systemd.device-timeout=30` — added by `banks-persist-setup` on first boot. Setup service uses `Wants=dev-disk-by\x2dpartlabel-UDA.device` to wait for NVMe enumeration.
    - **SSH persistence**: bind-mount individual `ssh_host_*` key files, NOT the entire `/etc/ssh/`. Binding the whole dir overwrites rootfs `sshd_config` with stale UDA copy, reverting UsePAM/KEX optimizations.
    - **Triggerhappy socket activation**: must be masked in kiosk image. Socket-activated `thd` ignores `--deviceglob` and waits for `th-cmd --passfd` from udev — keyboard hotkeys silently stop working.

18. **Tegra SoC audio is machine-gated.** A203 V2 carrier has no I2S DAC / DMIC / DSPK pins routed, so the Tegra audio crossbar (AHUB / ADMAIF / ADSP / I2S / DMIC / DSPK / AMX / ADX / SFC / MVC / MIXER / AFC / IQC / OPE / ARAD / ASRC) is **disabled** there via `no-audio-soc.cfg`. NVIDIA devkit P3509 (`jetson-xavier-nx-banks-devkit`) exposes I2S5 on the 40-pin header (DAP5: pin 12 BCLK, 35 LRCLK, 40 SDATA), so it gets the full SoC audio stack via `audio-soc.cfg`. Gating lives in `linux-tegra_%.bbappend`:
    ```
    SRC_URI:append:jetson-xavier-nx-a203          = " file://no-audio-soc.cfg"
    SRC_URI:append:jetson-xavier-nx-banks-devkit  = " file://audio-soc.cfg"
    ```
    - **Why off on A203**: ~17 modules loading during udev coldplug, plus `tegra210_adsp` driver iterating FE/BE DAI links from the generic `tegrasndt186ref` machine driver and spamming `Broken Path1 - FE not linked to BE` (~14 messages, ~3s of post-login log churn) because the DT enables ADSP DAI nodes that the machine driver doesn't pair with any BE on a board with no I2S routing. Even on devkit you'll still see this spam at boot — cosmetic only. Silence later via DT overlay (`status = "disabled"` on unused `tegra210-adsp-audio` children) or by demoting `dev_err → dev_dbg` in `tegra-alt/tegra210_adsp_alt.c:1409,1734`.
    - **A203 keepers**: `SND_HDA_TEGRA` (HDMI audio out), `SND_USB_AUDIO` (USB headsets), kernel core sound subsystem.
    - **Devkit + PCM5102A**: stock devkit DTS (`tegra194-p3668-all-p3509-0000.dts`) already enables `tegra_i2s5` with `I2S_DUMMY` codec on the `I2S_DAP` cell via `tegra194-audio-p3668.dtsi` + `tegra186-audio-dai-links.dtsi:1003`. PCM5102A is pin-strap configured (no I2C/SPI control bus), so **no DT overlay or codec node is required** — just turn the SoC audio kernel drivers back on and ALSA exposes card 1 "APE" with 20 ADMAIF PCMs + 2 ADSP FE devices. Header→PCM5102A pinout: 12→BCK, 35→LCK, 40→DIN; SCK→GND, XSMT→3.3V (un-mute — common silence cause), FLT/DEMP→GND. Route ADMAIF to I2S5 in XBAR before playing: `amixer -c APE cset name='I2S5 Mux' 'ADMAIF1'`.
    - **OPE specifically**: lives inside AHUB. Reaching OPE from software requires AHUB + ADMAIF (SW→HUB DMA gateway) + a physical output (I2S or DSPK). HDMI audio uses HDA, separate from AHUB. PipeWire feeds ADMAIF for HUB-routed paths.

## Open follow-ups

- ~~CAN0 500kbps~~ — **confirmed** UP at 500000bps, ERROR-ACTIVE, 0 bus errors at idle (via `jtx can`).
- ~~spidev exposed~~ — **confirmed** spidev0.0, spidev0.1, spidev2.0, spidev2.1 present.
- ~~Kiosk image~~ — **built and flashed**, weston + kiosk-shell + media player TUI working. BT A2DP streaming phone→Jetson→USB headset verified.
- ~~BT A2DP sink~~ — **working end-to-end**. Phone pairs, streams A2DP, PipeWire routes to USB headset (Sennheiser), AVRCP metadata + playback control via media player TUI.
- ~~WirePlumber ALSA~~ — **fixed**. USB audio devices now enumerated by PipeWire. Root cause: D-Bus ReserveDevice1 crash on headless.
- efi-timeout deb built; **not yet pushed to live board** (device was in UEFI menu). Push: `jtx push efi-timeout`.
- Full image rebuild to bake aptX/LDAC codec debs (libfreeaptx, libldac recipes present but not yet in a pushed image).
- ~~Touch support~~ — **fixed** with matchbox-terminal (GTK3/VTE). Mouse/touch working in media player TUI.
- projectM audio visualizer — recipes built (libprojectm + frontend-sdl2), GLES shader fixed, SDL2 PipeWire backend enabled. Presets need to be bundled into the image (currently pushed manually). ImGui overlay needs testing after GLES shader fix.
- Transparent terminal overlay for BT track info on top of projectM — matchbox-terminal supports VTE RGBA alpha, needs small patch.
- ~~I2S DAC wiring for actual audio output~~ — **enabled on devkit machine.** Stock devkit DTS wires I2S5 to 40-pin header; SoC audio kernel drivers re-enabled via machine-gated `audio-soc.cfg`. PCM5102A (pin-strap, no control bus) plugs straight in. A203 still routes to USB headset / BT A2DP (no I2S codec pinout on that carrier).
- Replace `debug-tweaks` (passwordless root) with proper user account once dev workflow settled.
- Plasma image (`kas/plasma.yml`) build not yet attempted. Expect KDE Plasma 6 Wayland via KWin; first time on Tegra so sharp edges likely.
- LXQt variant kas/recipe pair when ready.
- sstate mirror TODO.
- Boot time: `run-postinsts` adds ~2s on first boot. Deferred `pkg_postinst` scripts from poky packages (ldconfig, etc.) could be forced to run at image build time via `ROOTFS_POSTPROCESS_COMMAND` or ensuring all postinsts are build-time-safe. Low priority — only affects first boot, self-removes.

## Workflow preferences

- **Agent spawning**: always ask about worktree isolation before spawning an Agent (via `AskUserQuestion`), unless the user already specified for this session. Recommend worktree for multi-file rewrites/experiments; in-place for small focused edits.
- **Agent execution**: always spawn agents with `run_in_background: true` unless the user explicitly asks to wait.

## Memory

Auto-memory for this project lives at `~/.claude/projects/-mnt-yoctoworkspace-nx-car-jetson/memory/`. Three files there:
`user_banks.md`, `project_banks_jetson_linux.md`, `reference_kas_container_selinux.md`. They cover the same material as this CLAUDE.md but persist across all sessions, including ones outside this directory.
