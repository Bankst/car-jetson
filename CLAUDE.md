# Banks Jetson Linux

Yocto OE4T image targeting **Jetson Xavier NX production module on a SeeedStudio A203 V2 carrier**.
Built and flash-tested. Boot from eMMC, rootfs on external NVMe (M.2 Key M 2242).

> **Detailed docs** (deep dives split out of this file to keep it lean):
> - `docs/build-and-flash.md` — host setup, build, distributed icecc, flash, force-rebuild, lock recovery
> - `docs/bluetooth-audio.md` — BT A2DP sink stack, WirePlumber headless fixes, codecs
> - `docs/pipewire-eq-tooling.md` — PipeWire EQ/volume CLI scripts, filter-chain architecture, gotchas
> - `docs/kiosk-display.md` — Weston kiosk display stack, shell toggle, efi-timeout
> - `docs/devicetree-sd.md` — how DTB reaches kernel, custom DTB recipe, SD card CD GPIO
> - `docs/repo-workarounds.md` — 18 non-obvious workarounds (SELinux, GCC13, SSH latency, UDA persist, SoC audio gating, …)
> - `docs/cs42448-devkit-design.md` + sibling audio docs — CS42448 TDM codec design/bring-up

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
| Networking | NetworkManager (default) + systemd-networkd (available, masked from owning anything) | NM owns Ethernet/Wi-Fi/USB-gadget bridge; CAN0 at 500kbps via NM keyfile (NM 1.46+ has [can] section) |
| DE | Weston 10 kiosk (`kas/kiosk.yml`) or KDE Plasma 6 (`kas/plasma.yml`) | Decoupled — base image is DE-agnostic. Kiosk: built+flashed, working. Plasma: not yet built. |
| Bootloader | **`tegra-uefi-prebuilt`** (NVIDIA-shipped UEFI binary) | source-build EDK2 fails on scarthgap GCC 13 with GenFw PE-COFF errors; prebuilt is what NVIDIA ships |
| Storage | Boot/UEFI on QSPI/eMMC, rootfs on `/dev/nvme0n1p1` | `TNSPEC_BOOTDEV` in MACHINE conf, `EXTERNAL_ROOTFS_DRIVE=1` |

## Layer set

| Layer | Branch / Tag | Notes |
|---|---|---|
| poky | scarthgap | |
| meta-openembedded (oe, python, networking, multimedia, filesystems) | scarthgap | |
| meta-tegra | `scarthgap-l4t-r35.x` | (note the `.x` suffix) |
| meta-qt6 | tag `v6.9.0` | Plasma target only |
| yocto-meta-kf6 | `master` | Plasma target; LAYERSERIES_COMPAT override needed |
| yocto-meta-kde | `master` | Plasma target; ditto |
| meta-seeed-jetson | local | Our layer — see structure below |

## Repo / layer structure

```
scripts/
  setup-host-flash-permissions.sh   # polkit rule for unattended initrd-flash (udisksctl mount)
  audio/                            # PipeWire EQ/volume CLI prototypes — see docs/pipewire-eq-tooling.md

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
                                          # one-line shims around kf6_* (underscore) — upstream drift, not our bug
  recipes-bsp/
    tegra-bsp-a203/                       # ships A203 DTBs + pinmux from Seeed's JP 5.1.4 driver pack via deploy class
      files/                              # 3 DTBs + 1 pinmux .cfg, byte-for-byte from _input/203_jp514.tar.gz
    tegra-bootfiles/                      # pinmux substitution bbappend on meta-tegra's tegra-bootfiles
    banks-jetson-iface/                   # carrier-agnostic NX interface bring-up; via MACHINE_EXTRA_RDEPENDS (A203 + devkit)
      banks-usb-gadget.sh                 # configfs USB gadget: NCM ethernet on usb0, ACM serial on ttyGS0
      banks-usb-gadget.service            # systemd, runs at boot before NetworkManager
      l4tbr0.nmconnection                 # NM keyfile: bridge with method=shared (DHCP server on 192.168.55.1/24)
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
    linux-tegra_%.bbappend                # injects config fragments + DTB substitution at do_deploy
    linux-tegra/can.cfg                   # CAN_*, MTTCAN
    linux-tegra/spi.cfg                   # SPIDEV
    linux-tegra/usb-modem.cfg             # CDC_NCM, USB_SERIAL_OPTION, USB_WDM (cellular USB modems, off by default)
    linux-tegra/usb-gadget.cfg            # USB_GADGET, CONFIGFS_*, NCM/ACM/RNDIS/ECM
    linux-tegra/audio-soc.cfg             # devkit only: Tegra ASoC + I2S5 + AHUB + ADMAIF + ADSP + SPDIF
    linux-tegra/no-audio-soc.cfg          # A203 only: disable SoC audio (no I2S codec on carrier)
  recipes-core/
    banks-persist/                        # UDA partition persistence: format, mount /data, bind-mount BT + SSH keys
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
| `kas/kiosk.yml` | `banks-jetson-image-kiosk` | ✓ Built, flashed, working (weston + kiosk-shell + media player TUI; BT A2DP verified) |
| `kas/plasma.yml` | `banks-jetson-image-plasma` | Not yet built |
| `kas/lxqt.yml` | `banks-jetson-image-lxqt` | Placeholder |

## Build & flash (quickstart)

```sh
# SELinux escape is mandatory on Fedora enforcing.
KAS_BUILD_DIR=$PWD/build KAS_RUNTIME_ARGS="--security-opt label=disable" \
  kas-container build kas/base.yml
```

Flash: module in recovery (short FC REC to GND, power-cycle; `lsusb` → 0955:7e19), extract `…rootfs.tegraflash.tar.gz`, `sudo ./initrd-flash --erase-nvme`. After reboot SSH `ssh root@192.168.55.1` over USB CDC-NCM. **Full procedure + distributed icecc + force-rebuild → `docs/build-and-flash.md`.**

> icecc note: any build touching `linux-tegra` needs the `kas-icecc:4.7` container (`--network=host`, `-v /run/icecc:/var/run/icecc:rw`) — kernel `do_configure` fails without the icecc daemon. See build doc.

## Hardware notes

- No analog audio output on A203 V2 carrier — HDMI (card 0) and ADMAIF I2S (card 1, 20x XBAR-ADMAIF only). SoC audio machine-gated; see workaround #18 in `docs/repo-workarounds.md`.
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

## Status / confirmed working

- CAN0 500kbps — UP at 500000bps, ERROR-ACTIVE, 0 bus errors at idle (`jtx can`).
- spidev — spidev0.0/0.1, spidev2.0/2.1 present.
- Kiosk image — built+flashed; weston + kiosk-shell + media player TUI; BT A2DP phone→Jetson→USB headset verified.
- BT A2DP sink — working end-to-end (pair, stream, AVRCP metadata + control). See `docs/bluetooth-audio.md`.
- WirePlumber ALSA enumeration — fixed (D-Bus ReserveDevice1 crash on headless).
- Touch — fixed via matchbox-terminal (GTK3/VTE).
- I2S DAC output — enabled on devkit machine (PCM5102A pin-strap). A203 stays USB/BT.

## Open follow-ups

- efi-timeout deb built; **not yet pushed to live board**. Push: `jtx push efi-timeout`.
- Full image rebuild to bake aptX/LDAC codec debs (libfreeaptx, libldac recipes present, not yet in a pushed image).
- projectM audio visualizer — recipes built (libprojectm + frontend-sdl2), GLES shader fixed, SDL2 PipeWire backend enabled. Presets need bundling into image (currently pushed manually). ImGui overlay needs testing.
- Transparent terminal overlay for BT track info on top of projectM — matchbox-terminal supports VTE RGBA alpha, needs small patch.
- CS42448 TDM codec (8out/6in on I2S5) — **I²C control PROVEN on A203** (codec ACKs 0x48, `cs42xx8` reads CHIP_ID rev 4; DTB+pinmux correct). SoC-audio ALSA card NOT yet up: board runs old SoC-audio-OFF kernel Image (missing `snd_dmaengine_pcm_*` + ADSP) → **needs full flash of the audio-soc base image**, module-push dead-ends. DTB-deploy gotcha: L4TLauncher ignores extlinux `FDT`, loads the flashed `kernel-dtb` partition (A203 = `/dev/nvme0n1p3`). See `docs/cs42448-devkit-design.md` §10 bring-up status.
- PipeWire EQ → banks-frontend: wire `AudioMixer`/`AudioEQ` QObjects, ship `99-banks-eq.conf` recipe. See `docs/pipewire-eq-tooling.md` Next steps.
- Replace `debug-tweaks` (passwordless root) with proper user account once dev workflow settled.
- Plasma image (`kas/plasma.yml`) build not yet attempted (KDE Plasma 6 Wayland via KWin; first time on Tegra).
- LXQt variant kas/recipe pair when ready.
- sstate mirror TODO.
- Boot time: `run-postinsts` adds ~2s on first boot (deferred poky postinsts). Low priority — self-removes after first boot.

## Workflow preferences

- **Agent spawning**: always ask about worktree isolation before spawning an Agent (via `AskUserQuestion`), unless already specified for this session. Recommend worktree for multi-file rewrites/experiments; in-place for small focused edits.
- **Agent execution**: always spawn agents with `run_in_background: true` unless the user explicitly asks to wait.

## Memory

Auto-memory for this project lives at `~/.claude/projects/-mnt-yoctoworkspace-nx-car-jetson/memory/`:
`user_banks.md`, `project_banks_jetson_linux.md`, `reference_kas_container_selinux.md`. Persist across all sessions, including ones outside this directory.
