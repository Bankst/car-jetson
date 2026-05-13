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
| MACHINE | `jetson-xavier-nx-a203` | Custom; `requires jetson-xavier-nx-devkit-emmc.conf` and overrides storage + bootloader |
| DISTRO | `banks-jetson` | Banks's flavour, defined in `meta-seeed-jetson/conf/distro/banks-jetson.conf` |
| Init | systemd | |
| Networking | NetworkManager (default) + systemd-networkd (kept available for nothing in particular; networkd is currently masked from owning anything) | NM owns Ethernet/Wi-Fi/USB-gadget bridge; CAN0 brought up at 500kbps via NM keyfile (NM 1.46+ has [can] section) |
| DE | KDE Plasma 6 / KF6 / Qt6 (separate target `kas/plasma.yml`) | Decoupled — base image is DE-agnostic, swap to LXQt later by adding one packagegroup + one image recipe + one kas yaml |
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
kas/
  base.yml          # poky + meta-oe + meta-tegra + machine; DE-agnostic
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
    seeed-a203-iface/
      banks-usb-gadget.sh                 # configfs-based USB gadget setup: NCM ethernet on usb0, ACM serial on ttyGS0
      banks-usb-gadget.service            # systemd, runs at boot before NetworkManager
      l4tbr0.nmconnection                 # NM keyfile: bridge with method=shared (auto DHCP server on 192.168.55.1/24)
      l4t-gadget-usb0.nmconnection        # NM keyfile: usb0 as bridge slave
      can0.nmconnection                   # CAN0 at 500kbps
      seeed-a203-modules.conf             # /etc/modules-load.d/ — mttcan, can*, spidev
  recipes-kernel/linux/
    linux-tegra_%.bbappend                # injects 4 config fragments + DTB substitution at do_deploy
    linux-tegra/can.cfg                   # CAN_*, MTTCAN
    linux-tegra/spi.cfg                   # SPIDEV
    linux-tegra/usb-modem.cfg             # CDC_NCM, USB_SERIAL_OPTION, USB_WDM (cellular USB modems, off by default)
    linux-tegra/usb-gadget.cfg            # USB_GADGET, CONFIGFS_*, NCM/ACM/RNDIS/ECM
  recipes-core/
    images/banks-jetson-image-{base,plasma,lxqt}.bb
    packagegroups/{packagegroup-seeed-base, packagegroup-de-plasma-minimal}.bb

_input/
  203_jp514.tar.gz                        # Seeed's A203 driver pack for JP 5.1.4 (Xavier NX)
  extracted/                              # untracked; staging for the few files we actually use
  *_WRONG-BOARD-Orin-A603                 # the Orin tarball we got first by mistake; renamed for safety
```

## Build & flash

### One-time host setup

```sh
# Fedora 43 packages required for the flash step (NOT for the build — kas-container is hermetic)
sudo dnf install -y dtc vim-common gdisk bmap-tools cpp lz4

# Docker group access (after install + relogin to load group)
sudo usermod -aG docker "$USER"
# Either log out/back in fully (Plasma session inherits groups at login),
# OR wrap each command in: sg docker -c '...'
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

Adds remote node `bankst@10.0.10.45` (EPYC 7302P, 32t) as compile farm.
Local Ryzen 9800X3D contributes its 16t too. Aggregate ≈48t. Sustained
~40 parallel jobs during heavy compile bursts in testing.

Prereqs (one-time):
- `icecc-scheduler` + `iceccd` running on `10.0.10.45` with
  `ICECC_NETNAME=banks-yocto`, `ICECC_ALLOW_REMOTE=yes`,
  `ICECC_SCHEDULER_HOST=10.0.10.45`. Verify `ss -tlnp` shows ports 8765
  and 10245 listening. See `docker/REMOTE_ICECC_SETUP.md`.
- `iceccd` running locally with same netname + `SCHEDULER_HOST=10.0.10.45`.
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
sees the scheduler on the LAN. Monitor placement with `icemon` on the remote
box, or `ss -tn '( sport = :10245 )'` on `10.0.10.45` to count live jobs.
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

## Non-obvious workarounds in this repo (so future-you/me doesn't redo them)

1. **`KAS_RUNTIME_ARGS="--security-opt label=disable"`** is mandatory on Fedora enforcing. Without it the kas-container can't read `/repo` due to SELinux MCS labelling on bind mounts. Only mode that works is disabling label confinement for the container; `:Z` mount option fights kas-container's own `-v` lines.

2. **KDE/KF6 master branches drifted** — `yocto-meta-kde` does `inherit kf6-cmake` (hyphenated), `yocto-meta-kf6` ships `kf6_cmake.bbclass` (underscored). Our shims in `meta-seeed-jetson/classes/kf6-*.bbclass` bridge the gap. Remove if upstream syncs.

3. **`LAYERSERIES_COMPAT` override** for `qt6-layer`/`kf6`/`kde` lives in `meta-seeed-jetson/conf/layer.conf`, NOT in `local.conf`. The compat check runs after layer.confs but before local.conf is read. Their layer.confs claim "styhead walnascar" only, but KDE's own `yocto-manifest/scarthgap.xml` confirms master-on-scarthgap is the supported pairing.

4. **Bootloader: prebuilt UEFI** (`tegra-uefi-prebuilt`) instead of source-built EDK2. Source build of `edk2-firmware-tegra` on scarthgap GCC 13 fails with `GenFw: ERROR 3000: DOS header signature was not found in UiApp.dll` — PE-COFF conversion bug, likely `-flto` interaction. Prebuilt is what NVIDIA ships for L4T R35.6.4 and is well-validated.

5. **Don't `PACKAGECONFIG:remove "resolved"` from systemd** — it breaks `nss-resolve` which depends on it. NetworkManager and `systemd-resolved` coexist fine; resolved just provides DNS stub. We only remove `networkd` (and even that's currently *not* removed since we kept it available for the `l4t-usb-device-mode` recipe path, which we don't use anymore — the remove can be re-added if you want a leaner image).

6. **NetworkManager `nmtui` is gated by PACKAGECONFIG** — the `networkmanager-nmtui` package only exists if `nmtui` is in PACKAGECONFIG. We add it via `PACKAGECONFIG:append:pn-networkmanager = " nmtui"` in distro conf.

7. **A203 DTBs are byte-identical drop-ins** — Seeed kept NVIDIA's filenames (`tegra194-p3668-*.dtb`, `tegra19x-mb1-pinmux-p3668-a01.cfg`) so substitution is just "ship our copies; bbappend the kernel `do_deploy` to overwrite the kernel-built ones." Confirmed via md5: deployed DTB matches source 1:1.

8. **Cellular Quectel modem userspace + audio init script from the driver pack — DEFERRED.** Quectel is in `203_jp514.tar.gz` under `rootfs/leetop/quectel/`; A203 audio init is `code_spkmic.sh + startup.service`. Re-package both as recipes only if user actually needs them.

9. **USB gadget approach: NM owns the bridge.** We do *not* install meta-tegra's `l4t-usb-device-mode` recipe — it ships only systemd-networkd `.network`/`.netdev` files (and even then it's incomplete — the actual gadget creation script is missing from meta-tegra; NVIDIA ships it in their `nv-l4t-usb-device-mode` deb which meta-tegra doesn't pull in). Our setup: a small `banks-usb-gadget.sh` configfs script + systemd unit + NM keyfiles for the bridge, all under `recipes-bsp/seeed-a203-iface/`.

## Open follow-ups

- Verify CAN0 actually comes up at 500kbps on hardware (NM 1.46 `[can]` keyfile syntax — empirically untested for us yet).
- Verify `/dev/spidevX.Y` is exposed at runtime; depends on whether Seeed's A203 DTB enables an `spidev` child node on a SPI controller. If not, add a DT overlay.
- Replace `debug-tweaks` (passwordless root) with a proper user account once dev workflow is settled.
- Plasma image (`kas/plasma.yml`) build hasn't been attempted yet. Expect KDE Plasma 6 Wayland to work via KWin; first time on Tegra so sharp edges are likely.
- LXQt variant kas/recipe pair when ready.
- sstate mirror is still TODO. icecc plumbing landed (see "Build with distributed icecc" section); just needs `iceccd` + scheduler installed on `10.0.10.45` and locally to activate.

## Memory

Auto-memory for this project lives at `~/.claude/projects/-home-bankst-projects-nx/memory/`. Three files there:
`user_banks.md`, `project_banks_jetson_linux.md`, `reference_kas_container_selinux.md`. They cover the same material as this CLAUDE.md but persist across all sessions, including ones outside this directory.
