# Remaining Boot Optimization Work — Resume Here

Session paused 2026-05-20. Below is the actionable backlog. Group by status, then by priority.

## Active blocker: kernel build failing

Builds have been failing on `do_compile_kernelmodules` with:

1. **`xhci-tegra.ko` modpost: undefined references** to `xhci_irq`, `xhci_hub_control`, `xhci_urb_enqueue`, etc.
   - **Fix applied**: re-added `CONFIG_USB_XHCI_HCD=y` + `CONFIG_USB_XHCI_TEGRA=y` to
     `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/feature-adds.cfg` (bottom).
   - This had been added before in no-debug.cfg but got dropped when no-debug.cfg was rewritten to be dev-friendly.
   - **Verify next build clears this.**

2. **`scripts/Makefile.build:70: You cannot use subdir-y/m to visit a module Makefile. Use obj-y/m instead.`**
   - kbuild doesn't print which subdir is offending. Either a side effect of `no-bloat.cfg` disables or `feature-adds.cfg` adds.
   - **To debug**: drop fragments one at a time (start with `feature-adds.cfg`) and bisect.
   - May be resolved by the XHCI re-pin (some module's Makefile gates on USB_XHCI_HCD state).

3. **`'./nvmap.ko' will not be built even though obj-m is specified`** — likely downstream of (1) or (2). Re-verify after both fixed.

If build still fails after re-running with XHCI re-pin, drop `feature-adds.cfg` entirely temporarily to confirm it's the culprit:
```
sed -i 's|    file://feature-adds.cfg \\|# feature-adds disabled for bisect\n|' \
  meta-seeed-jetson/recipes-kernel/linux/linux-tegra_%.bbappend
```

## Pending tasks (from TaskList)

### High-impact, can do without target

| # | Task | Files | Est. save |
|---|---|---|---|
| **#9** | Port `PcdPlatformBootTimeOut=0` compile-time | EDK2 bbappend at `meta-seeed-jetson/recipes-bsp/uefi/edk2-firmware-tegra_%.bbappend`. Add hunk patching the EDK2 DSC default, ref `/tmp/build-comparison.md` (or `docs/boot-optimization-2026-05-20/build-comparison.md`) → MC2's `mc2-efi/custom/0001-efi-cleanup.patch`. | Defence-in-depth alongside live `efi-timeout` (which gets wiped by flash). Currently no time saved on already-tweaked devkit, but the new image's first boot will avoid losing Timeout=0. |
| **#10** | Remove `console=tty0` from kernel args | Edit `meta-seeed-jetson/conf/machine/jetson-xavier-nx-a203.conf` and `jetson-xavier-nx-banks-devkit.conf` to set `UBOOT_EXTLINUX_KERNEL_ARGS` without `console=tty0`. fbcon writes to HDMI framebuffer per dmesg line — slow. | Saves measurable time on every boot when HDMI is connected. |
| **#12** | `journald.conf RuntimeMaxUse=64M` | New file at `meta-seeed-jetson/recipes-core/systemd-conf/files/journald.conf` + bbappend at `meta-seeed-jetson/recipes-core/systemd-conf/systemd-conf_%.bbappend`. Ref `/home/bankst/microc/dev/mcx-monorepo/mcx-infrastructure/mc2-yocto-os/layers/meta-oxos-mc2/recipes-microc/systemd-conf/custom/journald.conf` | Bounded RAM. Not boot-time but good hygiene. |
| **#15** | Migrate `efi-timeout` → flash-time init-extra | Already written by agent. Verify next build picks up bbappend at `meta-seeed-jetson/recipes-bsp/efi-timeout/tegra-flash-init_%.bbappend` and ships `30-banks-efi-timeout.sh` to `/init-extra.d/`. | Survives flash wipes. Saves 0.3-0.8s/boot. |

### Pending live-target verification (after next successful flash)

| # | Task | Verify how |
|---|---|---|
| **#7** | `banks-persist-setup` systemd ordering cycle | If `#14` migration shipped successfully, the rootfs service is gone — cycle resolved automatically. Verify via `journalctl -b | grep ordering` on target. |
| **#8** | CAN0 bringup 3s delay | `mttcan can0: Bitrate set` was at kernel +13.9s, 3s after surrounding drivers. Inspect `can0-up.service` (in `meta-seeed-jetson/recipes-bsp/banks-jetson-iface/files/can0-up.sh`) and the NM keyfile (`can0.nmconnection`). Try replacing NM-managed CAN with direct `ip link set can0 up type can bitrate 500000` in a systemd unit (fires immediately, no NM negotiation). |
| **#14** | banks-persist-setup → init-extra | Agent shipped 50-format-uda.sh + bbappend. Verify after flash that /data mounts via static fstab (not via the old `banks-persist-setup.service`). |
| **#18** | NM-wait-online disable in image | Live tested earlier (saved 5.7s). Build agent baked it into `banks_bake_unit_fixes()`. Verify post-flash `systemctl is-enabled NetworkManager-wait-online.service` returns disabled. |

### Decision-only

| # | Task | Recommendation |
|---|---|---|
| **#6** | `pcie@14160000` DTB disable — Wi-Fi tradeoff | **Decided: keep enabled.** Wi-Fi card (QCA6174) is installed on devkit and link comes up cleanly when present. 1.1s "Phy link never came up" timeout only occurred when slot was empty. With card present, init takes ~6.5s total but is non-blocking. Adding `linux-firmware-ath10k` (in progress via firmware agent) will make Wi-Fi functional. |

## Investigations in flight at session end

- **Agent `a332838c4aa646317`**: adding `linux-firmware-ath10k` + `linux-firmware-iwlwifi` to `packagegroup-seeed-base.bb`. ATH11K NOT to be added (user instruction). Coordinator agent `ab5913302ccedf848` confirmed no firmware lines have been written yet — re-check on resume.

## Architectural notes

### What we shipped this session

- **Source-built EDK2 confirmed working** (CLAUDE.md note #4 was stale).
- **`uefi_jetson.bin` splash patch** baked: `meta-seeed-jetson/recipes-bsp/uefi/files/0001-banks-suppress-uefi-splash.patch` + bbappend. Saves ~7s pre-kernel (splash render loop).
- **Camera RTCPU + VI + sensors disabled**: `no-camera.cfg`. Saves ~20s kernel boot (Camera-FW load wait gone).
- **PC bloat trimmed**: `no-bloat.cfg`. ~60 disables (server NICs, InfiniBand, joysticks, PC sound).
- **Conservative `no-debug.cfg`**: only `CRYPTO_MANAGER_DISABLE_TESTS=y` + `# CONFIG_RCU_TRACE is not set`. Dev tools all kept.
- **`feature-adds.cfg`**: `CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER=y`, `NF_TABLES=m`, `HW_RANDOM=y`, `OVERLAY_FS=y`, `USB_NET_CDC_NCM=y`, **`USB_XHCI_HCD=y` + `USB_XHCI_TEGRA=y`** (just added to fix modpost). `ATH11K` dropped per user.
- **`banks-jetson-iface` recipe** renamed from `seeed-a203-iface`, carrier-agnostic. Devkit MACHINE conf adds CAN/SPI MACHINE_FEATURES + `MACHINE_EXTRA_RDEPENDS:append = " banks-jetson-iface"`.
- **`banks-usb-gadget.service` ordering fix** (live override + recipe pending): `WantedBy=sysinit.target` with `DefaultDependencies=no` and `After=sys-kernel-config.mount systemd-modules-load.service`. **Caveat**: caused a new `sysinit.target` ordering cycle on `systemd-journal-catalog-update.service` — may need to revert to `WantedBy=basic.target` with `After=local-fs.target`.
- **`banks-persist-setup` migration** to `50-format-uda.sh` init-extra script — agent shipped, pending build verify.
- **OP-TEE warning loop confirmed first-boot-only**: 15× → 1 on second boot. ~12s save automatic after first boot.

### What still hurts

From `scripts/uart-20260520-152448.log` boot timeline (kernel monotonic):

| Phase | Kernel time | Issue |
|---|---|---|
| 0 → 4.95s | 4.95s | driver init |
| 4.95 → 5.13s | 0.18s | PCIe ctrl 1 link up + NVMe — Wi-Fi card adds ~6.5s when present (firmware missing) |
| 6.93 → 7.56s | 0.63s | EXT4 mount → systemd start |
| 7.56s | — | **`local-fs.target: Found ordering cycle` on `banks-persist-setup.service`** — should resolve with #14 migration |
| 7.56 → 10.27s | 2.71s | Network rename `eth0→end0` + UDC enumeration |
| 10.27 → 10.71s | 0.43s | SPI tegra114 |
| 10.71 → 13.90s | **3.19s** | **CAN0 Bitrate set** — task #8 target |
| 13.90 → 16.80s | 2.90s | IRQ housekeeping + ADSP "Broken Path" spam |
| 16.80 → **37.86s** | **21.06s** | **Userspace (login)** — biggest chunk; NM-wait-online disable will drop ~5.7s, USB-gadget service move drops 9-10s, persist init-extra migration drops 1-3s. Estimated post-build: ~10s userspace. |

## Files of record

- `docs/boot-optimization-2026-05-20/README.md` — index of all 10 agent reports
- `docs/boot-optimization-2026-05-20/build-comparison.md` — vs mc2-yocto-os
- `docs/boot-optimization-2026-05-20/defconfig-diff-categorized.md` — 148 option diffs
- `docs/boot-optimization-2026-05-20/firstboot-comparison.md` — init-extra.d architecture
- `docs/boot-optimization-2026-05-20/optee-warning-investigation.md` — first-boot-only confirmation
- `docs/boot-optimization-2026-05-20/no-bloat-investigation.md` — PCI/server NIC trim
- `docs/boot-optimization-2026-05-20/cam-rtcpu-investigation.md` — camera disable
- `docs/boot-optimization-2026-05-20/easy-fixes-investigation.md` — three coordinated fixes
- `docs/boot-optimization-2026-05-20/usb-gadget-churn-investigation.md` — gadget service order
- `docs/boot-optimization-2026-05-20/uefi-splash-investigation.md` — splash patch
- `docs/boot-optimization-2026-05-20/edk2-investigation.md` — EDK2 source-build sanity

UART log snapshots in `scripts/uart-202605*.log`.

## Late discovery — l4tbr0 / usb0 NM flap

Symptom in `scripts/uart-20260520-154706.log` from kernel +38s onward: `l4tbr0: port 1(usb0)` cycles `blocking → disabled → blocking → forwarding → ready → disabled` every ~500ms forever.

Root cause: **two issues stacked.** (1) `dnsmasq` package was missing from image. NM's `method=shared` on `l4tbr0` (192.168.55.1/24) spawns dnsmasq for DHCP. (2) Even with dnsmasq installed, dnsmasq errored `directory /etc/resolv.conf for resolv-file is missing, cannot poll` on every "l4tbr0: link becomes ready" event — the missing /etc/resolv.conf caused dnsmasq to fail readiness, NM tore down the bridge, slave (usb0) cycled.

**Fixes applied**:
- Added `dnsmasq` to `meta-seeed-jetson/recipes-core/packagegroups/packagegroup-seeed-base.bb`. Distro conf already has `SYSTEMD_AUTO_ENABLE:pn-dnsmasq = "disable"` — NM spawns the binary directly, doesn't need the systemd unit.
- Added `/etc/resolv.conf → ../run/systemd/resolve/stub-resolv.conf` symlink in `banks_bake_unit_fixes()` of `banks-jetson-image-base.bb`. systemd-resolved is enabled so the stub populates at boot.

Verify post-flash: `ssh root@192.168.55.1 'systemctl is-active dnsmasq; ip -br addr show l4tbr0'` should show inactive (good — only spawned by NM) + 192.168.55.1/24.

## CRITICAL ROOT CAUSE — `banks-persist-setup.service` ordering cycle is the keystone

Found end of session: a single ordering cycle in the running image cascades into apparent unrelated failures. Journal (May 29 image) shows:

```
systemd-resolved.service: Found ordering cycle on systemd-tmpfiles-setup.service/start
  Found dependency on local-fs.target/start
  Found dependency on banks-persist-setup.service/start
  Found dependency on basic.target/start
  Found dependency on sockets.target/start
  Found dependency on dbus.socket/start
  Found dependency on sysinit.target/start
  Found dependency on systemd-resolved.service/start
Job systemd-tmpfiles-setup.service/start deleted to break ordering cycle starting with systemd-resolved.service/start
systemd-resolved.service: Failed to spawn 'start' task: Not a directory
systemd-resolved.service: Failed with result 'resources'.
```

Chain of consequences:

1. `banks-persist-setup.service` creates ordering cycle on local-fs.target ← root
2. systemd deletes `systemd-tmpfiles-setup.service` to break cycle
3. tmpfiles never runs → `/run/systemd/resolve/` directory never created
4. systemd-resolved start fails (`RuntimeDirectory=systemd/resolve` can't be created → "Not a directory")
5. `/etc/resolv.conf` → `/etc/resolv-conf.systemd` → `/run/systemd/resolve/resolv.conf` — dangling symlink chain
6. NM activates l4tbr0 (method=shared) → spawns dnsmasq → dnsmasq inotify on resolv.conf parent dir fails → reports "directory ... missing, cannot poll"
7. dnsmasq fails readiness → NM tears down bridge → usb0 slave thrashes every ~500ms
8. Host can't get 192.168.55.x DHCP → SSH dies → "lots of up/down on usb0" symptom

**ALL of the following are downstream of #1:**
- usb0 flap
- SSH unreachability at 192.168.55.1
- dnsmasq resolv-file errors
- bridge port disabled/forwarding cycling

The fix for #1 lands in the queued 16:08 build via the init-extra migration (banks-persist-setup.service is GONE in the new recipe; UDA format moved to flash-time `50-format-uda.sh`). **Flash the 16:08 artifact and the entire cascade disappears.**

Live mitigations attempted on existing image (before flash):
- `systemctl mask banks-persist-setup.service` then reboot → resolved still failed (likely `banks-persist-bind.service` or auto-generated `data.mount` still in the chain)
- Try `systemctl mask banks-persist-setup.service banks-persist-bind.service data.mount && reboot` for clean break

## Late-session recipe changes (NOT in 16:08 artifact)

| File | Change |
|---|---|
| `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/usb-gadget.cfg` | Trimmed to NCM + ACM only. Disabled `RNDIS`, `ECM`, `ECM_SUBSET`, `EEM`, **`MASS_STORAGE`**. f_mass_storage was being re-probed on USB cable reset, adding multi-second xudc re-enumeration on every cable jiggle. |
| `meta-seeed-jetson/recipes-core/packagegroups/packagegroup-seeed-base.bb` | Added `dnsmasq` (NM `method=shared` needs the binary). Removed `linux-firmware-ath11k` (user decision). Kept `linux-firmware-ath10k` + `linux-firmware-iwlwifi`. |
| `meta-seeed-jetson/recipes-core/images/banks-jetson-image-base.bb` | Added `/etc/resolv.conf → ../run/systemd/resolve/stub-resolv.conf` symlink in `banks_bake_unit_fixes()`. (May be redundant with poky's default — verify on flash.) |
| `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/feature-adds.cfg` | Removed `ATH11K=m` + `ATH11K_PCI=m`. Added `CONFIG_USB_XHCI_HCD=y` + `CONFIG_USB_XHCI_TEGRA=y` pin for modpost. |
| `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/no-debug.cfg` | **RESTORED CONSERVATIVE** version (user wants all dev/debug/tracing kept). Only disables `CRYPTO_MANAGER_DISABLE_TESTS=y` + `# CONFIG_RCU_TRACE is not set`. Plus modpost-defense pins `CONFIG_USB=y`, `USB_XHCI_HCD=y`, `USB_XHCI_TEGRA=y`. Heavy version that agent shipped in 16:08 build is wrong. |

## In 16:08 artifact (what shipped)

- Camera RTCPU disabled (`no-camera.cfg`)
- PC driver bloat trimmed (`no-bloat.cfg`)
- UEFI splash patch (3 calls suppressed in PlatformBm.c)
- **HEAVY** `no-debug.cfg` (28 disables — kills ftrace/perf/kgdb/SysRq)
- NM-wait-online masked
- banks-persist migrated to flash-time init-extra (the critical fix for the cascade above)
- efi-timeout migrated to flash-time init-extra
- banks-jetson-iface (renamed, carrier-agnostic; CAN/SPI/gadget on devkit + A203)
- `feature-adds.cfg` v1: FB_DEFERRED_TAKEOVER + NFT + HW_RANDOM/OVERLAY_FS/USB_NET_CDC_NCM builtins + ATH11K (since dropped) + XHCI builtin re-pin
- USB-gadget service `WantedBy=sysinit.target` (NOTE: introduced new ordering cycle on systemd-journal-catalog-update.service — needs revert to basic.target with `After=local-fs.target`)

## Decision on flashing

Two paths:

**A. Flash 16:08 NOW (one button-press)**: 
- ✓ Fixes the banks-persist cascade → resolved/dnsmasq/usb0/SSH all stable
- ✗ Heavy no-debug.cfg installed (no ftrace/perf for dev work)
- ✗ Cable jiggle still triggers multi-second xudc storm (MASS_STORAGE still built)
- ✗ No dnsmasq for cleaner NM shared-method behavior (resolved fix should suffice without it though)
- ✗ Wi-Fi card non-functional (no firmware)
- ✗ USB-gadget sysinit.target ordering cycle still present

**B. Rebuild first (~5 min)**:
- ✓ Conservative no-debug (dev tools intact)
- ✓ Trimmed gadget (no MASS_STORAGE/RNDIS/etc — cable jiggle fast)
- ✓ dnsmasq + resolv.conf symlink
- ✓ ATH11K dropped properly
- ✓ ath10k + iwlwifi firmware (Wi-Fi works)
- Still has: USB-gadget sysinit.target cycle (not yet reverted)
- Sstate mostly hit; only kernel reconfigure + image rootfs assembly run.

Recommend B unless you need to test the cascade fix in isolation right now.

## Continuation checklist

1. **Resume build.** Run:
   ```
   KAS_BUILD_DIR=$PWD/build KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
     kas-container --runtime-args "--security-opt label=disable" \
     --runtime-args "--network=host" --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
     build kas/devkit.yml
   ```
   The XHCI re-pin should clear the modpost error. If `subdir-y/m` error persists, bisect by temporarily removing `feature-adds.cfg` from SRC_URI in `linux-tegra_%.bbappend`.

2. **Verify firmware agent landed** ath10k/iwlwifi packages in `packagegroup-seeed-base.bb`. If not, add manually:
   ```
   RDEPENDS:${PN} += "linux-firmware-ath10k linux-firmware-iwlwifi"
   ```

3. **Flash the build** and capture UART. Goal: confirm:
   - No more `Camera-FW` lines
   - No more `Phy link never came up` (when Wi-Fi present)
   - No more `local-fs.target: ordering cycle`
   - `NetworkManager-wait-online.service` not running
   - `/data` mounted via fstab, not via service
   - Login at < ~30s total power-on

4. **Tackle tasks #9, #10, #12** (PcdPlatformBootTimeOut, console=tty0 removal, journald cap) — purely recipe changes, no target needed.

5. **Tackle #8 (CAN0 3s)** by replacing NM-managed CAN with a direct `ip link set` systemd unit.

6. **Decide on usb-gadget service ordering**: revert from `sysinit.target` (which caused journal-catalog cycle) to `basic.target` with `After=local-fs.target` — cleaner.
