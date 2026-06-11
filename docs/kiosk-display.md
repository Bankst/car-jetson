# Kiosk image — display stack + efi-timeout

## Display stack notes

L4T R35 (JP5) dropped the X11 DDX driver. `/dev/dri/card0` is `tegra_udrm` — NVIDIA's GPU compute device with no KMS connectors. Xorg `xf86-video-modesetting` finds it but fails with "no screens found". **Wayland via EGL/GBM is the only display path.**

- **Weston version**: meta-tegra forces `PREFERRED_VERSION_weston = "10.0%"` in `tegra-common.inc` → meta-tegra's 10.0.2 wins over poky's 13.
- **Display wiring**: `meta-tegra/conf/layer.conf` remaps `libdrm → libdrm-nvdc → tegra-libraries`. Weston's DRM backend talks to NVIDIA NVDC automatically.
- **Launcher**: `kiosk.service` (`Restart=always`) runs `/etc/kiosk/kiosk-launcher`, which starts weston (`XDG_RUNTIME_DIR=/run/kiosk`, socket `wayland-kiosk`), waits for socket, then execs `/etc/kiosk/kiosk-app`.
- **Shell toggle**: `kiosk-launcher` checks `/var/lib/kiosk/desktop-mode` flag: absent = `kiosk-shell.so` (no panel, fullscreen), present = `desktop-shell.so` (top bar, `Super+Tab` for window switching). Toggled via `Ctrl+Shift+D` (triggerhappy daemon, `kiosk-keys.conf`). Toggle script uses flock debounce (NanoKVM sends duplicate key events from two kbd devices).
- **Default kiosk app**: `banks-media-player` — Python curses TUI showing BT AVRCP metadata (title/artist/album/progress) with play/pause/next/prev/volume controls. Keys: `space` play/pause, `n`/right next, `p`/left prev, `up`/`+` vol up, `down`/`-` vol down, `m` mute. Uses `dbus-monitor` thread for near-instant track updates + `dbus-send` for commands + `wpctl` for volume.
- **Desktop mode app**: `weston-terminal --maximized` with bash login shell (sources `/etc/profile`).
- **Idle/lock disabled**: `weston-kiosk.ini` sets `idle-time=0`. Without this, weston's screen locker activates after 5 min.
- **weston-init masked**: kiosk image masks `weston.service` + `weston.socket` from `weston-init` recipe to prevent compositor conflict on tty7.
- **Mouse/touch**: weston-terminal 10 does NOT support mouse reporting (no DECSET 1000/1006). Touch-enabled TUI uses `matchbox-terminal` (GTK3 + VTE, Wayland-native, proper mouse reporting). `foot` terminal not in any OE layer.
- **PACKAGE_ARCH**: `packagegroup-kiosk` must set `PACKAGE_ARCH = "${MACHINE_ARCH}"`. Allarch packagegroups can't depend on dynamically-renamed NVIDIA packages (e.g. `egl-gbm → libnvidia-egl-gbm`).
- **GPU check**: `WAYLAND_DISPLAY=wayland-kiosk XDG_RUNTIME_DIR=/run/kiosk weston-info` or `glmark2-wayland` (deb available, push with `jtx push glmark2`).

## efi-timeout — UEFI boot countdown

`meta-seeed-jetson/recipes-bsp/efi-timeout/` — first-boot oneshot service. Writes `Timeout` EFI variable (UINT16=0) to QSPI via efivarfs, eliminating the L4TLauncher countdown. Stamps `/var/lib/efi-timeout-configured` so it only runs once. In `packagegroup-seeed-base` — all images get it. Push to live board: `jtx push efi-timeout`.
