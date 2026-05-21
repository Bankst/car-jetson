# Build comparison — mc2-yocto-os vs car-jetson

Reference: `/home/bankst/microc/dev/mcx-monorepo/mcx-infrastructure/mc2-yocto-os` (OXOS MC2 / Rishab Nayak)
Ours:      `/mnt/yoctoworkspace/nx/car-jetson` (Banks)

Both are **Yocto scarthgap + meta-tegra `scarthgap-l4t-r35.x`** with the same `linux-tegra 5.10` BSP and prebuilt-UEFI/source-EDK2 conventions, so deltas are portable. Reference is _not_ a different distro. Their layer is `meta-oxos-mc2`; ours is `meta-seeed-jetson`. Differences below are concrete, ranked by likely boot/quality impact.

Their build also drags in Mender (A/B + dual rootfs), Tailscale, FIPS, OpenTelemetry, samhain, mc2 cassette firmware, framos camera stack, Silex Wi-Fi/BT — these are productisation choices unrelated to our headless A203 use case. Ignore those.

---

## 1. Kernel config

| Item | Reference | Ours | Action | Impact |
|---|---|---|---|---|
| Defconfig strategy | Full custom `tegra_defconfig` (8613 lines) shipped via `recipes-kernel/linux/custom/tegra_defconfig` + `unset KBUILD_DEFCONFIG`. 3768 explicit `is not set` lines. | Upstream meta-tegra `tegra_defconfig` + 8 fragments (`can.cfg`, `spi.cfg`, `usb-modem.cfg`, `usb-gadget.cfg`, `dt-overlay.cfg`, `overlayfs.cfg`, `no-camera.cfg`, `no-bloat.cfg`) | **Keep fragment approach** — defconfig fork drifts violently on every NVIDIA bump | n/a |
| `CONFIG_PREEMPT=y` | Yes | Probably stock (`PREEMPT_VOLUNTARY`) | **Evaluate** — gives lower latency for media/audio (BT A2DP) but small throughput cost | Latency win |
| `CONFIG_HZ=250` (vs stock 100/1000) | 250 | Stock | Skip; minor | None |
| Tracing/perf bloat: `TRACEPOINTS`, `IKHEADERS`, `DEBUG_FS_ALLOW_ALL`, `KALLSYMS`, `KALLSYMS_ALL`, `DYNAMIC_DEBUG`, `BPF_PRELOAD`, `USERFAULTFD`, `SCHED_AUTOGROUP`, `CHECKPOINT_RESTORE`, `UCLAMP_TASK`, `PSI`, `BOOT_CONFIG`, `RCU_EXPERT`, `CGROUP_RDMA/DEBUG`, `SLUB_MEMCG_SYSFS_ON`, `CC_OPTIMIZE_FOR_SIZE`, `COMPAT_BRK`, `SLAB/SLOB`, `BPF_JIT_ALWAYS_ON` | Disabled | Stock (mostly y) | **Port selective set** to `no-bloat.cfg` — `TRACEPOINTS`, `IKHEADERS`, `BPF_PRELOAD`, `USERFAULTFD`, `CHECKPOINT_RESTORE`, `SCHED_AUTOGROUP`, `KALLSYMS_ALL` are safe wins | Image size + small boot time win |
| All `ARCH_*` non-Tegra (Actions, Sunxi, Alpine, BCM, Berlin, Bitmain, Brcmstb, Exynos, Sparx5, K3, Layerscape, LG1K, Hisi, Meson, MMP, Mediatek, Mvebu, MXC, NPCM, Omap, QCom, Realtek, Renesas, Rockchip, Seattle, Socfpga, Stratix, S5P, Stm32, Tegra-others, Uniphase, Vexpress, Visconti, ZX, Zynqmp) | All disabled | Stock has many `=y` | **Port** — wholesale ARCH_* disable in `no-bloat.cfg`. Pure dead code on Xavier NX. | Small image size; no boot-time effect |
| `CONFIG_TRACEPOINTS` | not set | y | **Port** — significant: kills ftrace ring buffers + most kernel debug infra | RAM saving, marginally faster | 
| `CONFIG_PC104` | not set | y | Port | Trivial |
| `CONFIG_COMPAT_BRK` | not set | varies | Port | Trivial |
| `CONFIG_MODULE_SIG=y` + `MODULE_SIG_ALL/SHA256` | Yes (FIPS-driven) | No | **Ignore** — they enforce signed modules for FIPS; we don't | Boot cost from signing |
| Kernel module autoload: `spidev` | Yes (in bbappend) | We already do `KERNEL_MODULE_AUTOLOAD` for `seeed-a203-modules.conf` via modules-load.d | Equivalent | None |
| Module-signing disable removal | `SRC_URI:remove = "file://disable-module-signing.cfg"` | n/a | Ignore (FIPS) | None |

**Recommended action: write `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/no-dev-bloat.cfg`** that disables:
```
TRACEPOINTS, IKHEADERS, BPF_PRELOAD, BPF_JIT_ALWAYS_ON, USERFAULTFD,
CHECKPOINT_RESTORE, SCHED_AUTOGROUP, KALLSYMS_ALL, KALLSYMS_BASE_RELATIVE,
UCLAMP_TASK, PSI, BOOT_CONFIG, RCU_EXPERT, CGROUP_RDMA, CGROUP_DEBUG,
SLUB_MEMCG_SYSFS_ON, DEBUG_RSEQ, PC104, COMPILE_TEST, USELIB,
GENERIC_IRQ_DEBUGFS, DEBUG_PERF_USE_VMALLOC, SGETMASK_SYSCALL,
SYSFS_DEPRECATED, BPF_UNPRIV_DEFAULT_OFF, SLUB_DEBUG,
plus all ARCH_<non-Tegra>
```
Expected impact: smaller vmlinuz, lower memory pressure for trace machinery; no measured boot time gain larger than ~tens of ms.

---

## 2. EDK2 / UEFI

| Item | Reference | Ours | Action |
|---|---|---|---|
| Splash suppression (`BootLogoEnableLogo` + `DisplaySystemAndHotkeyInformation`) | Patched out in `PlatformBm.c` (0002-efi-cleanup.patch) | Patched out AND `MemoryTest()` also skipped in `0001-banks-suppress-uefi-splash.patch` | Ours is **superset**, keep ours |
| `Print(L"** WARNING: Test Key is used **")` in BdsEntry | Patched out (`MdeModulePkg/.../BdsEntry.c`) | Not patched | **Port** — comment out the Print line in BdsEntry.c. Saves serial spam during BDS, especially on first boot. Trivial 1-liner. |
| L4TLauncher `ErrorPrint(L"Attempting GRUB Boot")` and `Attempting Direct Boot` | Patched out | Not patched | **Port** — same trivial cosmetic patch in `L4TLauncher.c`. Two lines. |
| `PcdPlatformBootTimeOut` default in `NVIDIA.common.dsc.inc` | Patched to **0** at compile time | We use a runtime `efi-timeout` oneshot that writes `Timeout=0` efivar on first boot | **Adopt theirs** as additional defence — compile-time PCD default of 0 means it's correct even before our first-boot oneshot runs, AND if a user deletes the efivar. Two-place defence in depth. |
| Kernel EFI stub messages ("Booting Linux Kernel...", "Using DTB from configuration table", "Loaded initrd from...", "Exiting boot services...") | Patched out in `drivers/firmware/efi/libstub/*` | Not patched | **Port** — kernel patch `0001-efi-cleanup-and-powergate-modifications.patch`. Cosmetic but removes 4 serial console lines between L4TLauncher and userspace. |
| BPMP powergate `disp/dispb/dispc` → `GENPD_FLAG_ALWAYS_ON` | Patched (same file) | Not patched | **Evaluate** — they keep the display power-domain always on. Useful only if HDMI is wired and unplug/replug churn matters. On headless A203 = irrelevant. **Skip.** |
| EDK2 SRC_URI in `recipes-bsp/uefi/edk2-firmware-tegra_%.bbappend` | Currently empty (`FILESEXTRAPATHS` only; their custom patches live in a parallel `recipes-microc/mc2-efi/` bbappend) | Single splash patch via SRC_URI | Our layout is cleaner; reuse it for adding the two ported patches above |

---

## 3. Tegra initrd / bootfiles

| Item | Reference | Ours | Action |
|---|---|---|---|
| Custom initrd script | `tegra-minimal-init/custom/init-boot.sh` — a stripped-down rootfs mount + switch_root with platform-preboot/pre-switchroot hooks | Stock meta-tegra | **Evaluate** — could shave a small amount off rootfs handoff time. Their script is minimalist (62 lines, mounts proc/dev/sys + efivarfs, parses cmdline, mounts root, switch_root to preinit). Stock is similar. Low priority. |
| `tegra-bootfiles` pinmux substitution | Standard meta-tegra (no bbappend) | Our `tegra-bootfiles_%.bbappend` substitutes A203 pinmux | We need ours, no change |
| Storage layout patch | `tegra-storage-layout-base_%.bbappend` rewrites UDA allocation attribute to `0x808` and moves UDA partition to end (before secondary_gpt), strips `id="1/2/3"` from `<partition>` tags | Stock | **Evaluate** — this is for Mender A/B layout. Not relevant to us unless we add A/B. **Skip.** |
| `tegra-pinmux-dts2cfg-native` workdir race fix | They add `do_unpack[depends]`, `do_recipe_qa[depends]` to serialize against `tegra-binaries` | None — we haven't hit this race | **Bookmark** — if we ever see "Linux_for_Tegra/ wiped mid-build" errors, copy the bbappend in `tegra-binaries/tegra-pinmux-dts2cfg_%.bbappend` |
| `tegra-eks-image-base` deltas | Same workdir serialization, plus `deltask do_populate_lic` | None | Same as above — bookmark |
| `efi-timeout` analogue | n/a (they use compile-time PCD) | Our `recipes-bsp/efi-timeout/` first-boot oneshot | Keep ours; consider also porting their compile-time PCD as additional defence |

---

## 4. Image / packagegroup

| Item | Reference | Ours | Action |
|---|---|---|---|
| Base image package set | Heavyweight: NM, sudo, dnsmasq, hostapd, tailscale, mender, framos, microc apps, nftables, iptables, rsyslog, samhain, gdb, evtest, otel-collector, fuse-exfat, run-postinsts, postgresql in some images, opencv 4.5.2 | Lean: `packagegroup-core-boot + packagegroup-base + packagegroup-seeed-base` | Ours is correctly minimal. **No change.** |
| `xterm` in base image | Yes | No | Skip |
| Removed `target-sdk-provides-dummy` from SDK toolchain | Yes | n/a | Skip (SDK build only) |
| `tegra-mender-setup mender-full extrausers cve-check create-spdx` inherits | Yes | No (we don't bother with cve-check/spdx in dev) | **Evaluate** — `cve-check` is cheap to enable and produces useful reports during builds. Possibly port. |
| Persistent logs (`VOLATILE_LOG_DIR = "no"`) | Yes | We use volatile (default) | **Evaluate** — they go persistent; we likely want that on the A203 too (UDA partition is there) |
| `PACKAGECONFIG:remove:pn-systemd = "timesyncd"` (Chrony only) | Yes | We use timesyncd | **Skip** — timesyncd is fine for our use; Chrony would add `chrony` userspace daemon |
| `KERNEL_ARGS:append = " quiet"` + `KERNEL_ARGS:remove = " console=tty0"` | Yes | We probably still have tty0 console. Check `cat /tmp/jetson-boot-profile/cmdline.txt` | **Port** — adding `quiet` and removing `console=tty0` suppresses dmesg from the framebuffer console (HDMI), eliminates console-spew dump on boot. Easy win for cleaner display experience even on headless serial. |
| Default `NVPMODEL_CONFIG_DEFAULT = "2"` (15W desktop) | Yes | Default (=0, 10W 2-core probably) | **Evaluate** — power-mode 2 is "15W 6-core max" on Xavier NX. If we want max perf out of the box, port. For battery-powered uses, leave at default. |
| `EXTRA_USERS_PARAMS` creates `gpiod`, `spi`, `i2c` groups + `microc` user (passworded), plus root pw | Yes | We rely on `debug-tweaks` (empty root pw) | **Evaluate later** — when we move off debug-tweaks, port their groups-and-user pattern |

---

## 5. Systemd services / masks / drop-ins

| Item | Reference | Ours | Action |
|---|---|---|---|
| `systemd-networkd` mask | _Not masked_ — they leave networkd available (use NM though) | Masked (we found it added ~100s boot delay) | **Keep our mask** — they may be paying this cost unknowingly |
| `sshd.socket` mask + pre-forked sshd | _Not masked_ — they use stock sshd via openssh recipe (their sshd_config keeps PAM, FIPS-grade KEX/Ciphers, no `UsePAM no`) | Masked socket; we run pre-forked sshd with `UsePAM no` | **Keep ours** — ~1.65s login latency win per CLAUDE.md note 14. Theirs is the high-security/FIPS posture; ours is the low-latency dev posture. |
| `nvs-service.service` `network-online.target` drop-in | _None_ | We add `Wants=` clear and `After=nvstartup.service` | **Keep ours** |
| Serial console on `ttyTHS0` | Stock (no special handling) | We enable `serial-getty@ttyTHS0` + add to securetty | Keep ours |
| Custom services that run on boot: `setup-swapfile`, `set-hostname`, `toggle-ssh-password-auth`, `log-cleanup.timer`, `rtc-hctosys`, `save-good-time`, `iptables`, `samhain`, `integritycheck`, `mender-store-permissions` | Theirs | None of these are relevant to us | Skip |
| `rtc-hctosys.service` + `save-good-time.service` | Yes — pair that initialises system clock from RTC very early (sysinit.target), and persists a "last known good time" to disk on shutdown. Useful when no NTP is available. | None — we rely on timesyncd | **Evaluate** — for offline/air-gapped boots this pattern is gold. Their scripts are 2 shell files. **Worth porting** if we expect offline boots. |
| `log-cleanup.timer` | rotates logs periodically | journald handles this for us | Skip |
| `journald.conf` override (`Storage=volatile`, `ForwardToSyslog=yes`, `RuntimeMaxUse=64M`) | Yes | Default (auto / persistent) | **Evaluate** — they go all-volatile (RAM only). Reduces flash wear on UDA partition. We currently use default storage (auto → persistent once /var/log/journal exists). Their `64M` cap is a fine guardrail. **Port the `RuntimeMaxUse=64M` line at minimum.** |

---

## 6. Boot args / kernel cmdline / efivars

| Item | Reference | Ours | Action |
|---|---|---|---|
| `console=tty0` | Removed via `KERNEL_ARGS:remove` | Probably still present | **Port** — see §4 |
| `quiet` | Appended | Not appended | **Port** — see §4 |
| `fips=1` | Appended | No | Skip (FIPS) |
| Default Timeout efivar | Set to 0 at compile time via PCD | Set at first boot via efi-timeout oneshot | **Port theirs as defence-in-depth** alongside keeping ours |
| BootLogo efivar | n/a | n/a | None |

---

## 7. Device tree

| Item | Reference | Ours | Action |
|---|---|---|---|
| Custom DTB | None apparent in their meta-oxos-mc2; rely on meta-tegra stock | Custom `seeed-a203-devicetree` recipe + DTB ships at `/boot/devicetree/tegra194-p3668-a203.dtb`, referenced via `FDT` in extlinux.conf | We're ahead here. Keep ours. |
| `KERNEL_DEVICETREE` overrides | Not in oxos layer | Set via machine conf | Keep |
| Disable unused PCIe nodes | _Not done_ | _Not done_ | **Evaluate** — neither of us disables `pcie@14160000` etc. Could be a small probe-time win for nodes that have nothing wired. Low priority. |
| HDMI sysfs additions (DDC power toggle, force EDID reread) | Two custom kernel patches add `ddc_power_toggle` and `force_edid_reread` sysfs attrs on the tegra-dc platform device | None | **Skip** — they need HDMI runtime power control for a connected display; A203 headless doesn't care. |

---

## 8. Tegra firmware blobs

| Item | Reference | Ours | Action |
|---|---|---|---|
| `TNSPEC_BOOTDEV` | `nvme0n1p1` | `nvme0n1p1` (set in MACHINE conf) | Equivalent |
| QSPI / MB1 / MB2 / cboot prebuilts | Stock meta-tegra | Stock + `tegra-uefi-prebuilt` | Equivalent |
| Custom storage layout (UDA at end, `0x808` alloc attribute) | Yes | Stock | Skip unless we adopt Mender |

---

## 9. NetworkManager / udev

| Item | Reference | Ours | Action |
|---|---|---|---|
| NM PACKAGECONFIG | `nmtui dhcpcd dnsmasq wifi` | `nmtui` (per CLAUDE.md note 6) | **Port** — adding `dhcpcd` (NM's preferred DHCP client over the internal one) + `dnsmasq` (for shared-mode IP allocation on usb0 bridge) + `wifi`. Their `NETWORKMANAGER_DHCP_DEFAULT = "dhcpcd"`. Wifi is already implicit; dhcpcd is the meaningful one. **Evaluate** dhcpcd impact on USB-NCM bridge stability. |
| Persistent net rules (rename eth0→det0 / eth1, wlan0) | `10-persistent-net.rules` (MAC-prefix `48:b0:2d:*` → det0) | None | **Skip** — their rule is hardware-specific (their MAC) |
| GPIO/SPI/I2C/TTY rules to assign groups | `20-gpiod.rules`, `30-spidev.rules`, `40-i2c.rules`, `50-tty.rules` | Some, in `seeed-a203-iface` | **Compare and merge** — when we move off `debug-tweaks` to a real `bankst`/`microc` user, we'll want their group-assignment pattern |
| Auto-mount script + ignorelist for `/dev/nvme0n1*`, `/dev/mmcblk0*`, `/dev/loop`, `/dev/ram`, `/dev/mtdblock`, `/dev/md`, `/dev/dm-*` | `mount.sh` + `mount.ignorelist` | None — we rely on udev-extraconf default | **Evaluate** — their ignorelist prevents auto-mounting of system devices (nvme0n1 is the rootfs!). If we ever plug a USB stick, our default might try to mount system devices that match. **Port `mount.ignorelist`** as a defensive measure if we add `udev-extraconf` to images. |

---

## Top recommendations, ranked

1. **Port the EDK2 `Timeout=0` PCD compile-time default** to our `edk2-firmware-tegra_%.bbappend`. Defence-in-depth alongside `efi-timeout` runtime.
2. **Port the L4TLauncher "Attempting GRUB Boot" / "Attempting Direct Boot" suppression**, the BdsEntry "Test Key is used" suppression — three trivial Print-comment-outs that clean up serial output during BDS.
3. **Port the kernel EFI stub message suppressions** (`0001-efi-cleanup-and-powergate-modifications.patch`, minus the powergate hunk). Removes 4 lines of serial output between L4TLauncher and userspace.
4. **Port `KERNEL_ARGS:append = " quiet"` and `KERNEL_ARGS:remove = " console=tty0"`** to `banks-jetson.conf`. Combined with above, this eliminates virtually all dmesg-to-display spam during boot.
5. **Add a `no-dev-bloat.cfg` kernel fragment** covering `TRACEPOINTS, IKHEADERS, BPF_PRELOAD, USERFAULTFD, CHECKPOINT_RESTORE, SCHED_AUTOGROUP, KALLSYMS_ALL, UCLAMP_TASK, PSI, BOOT_CONFIG, CGROUP_RDMA, CGROUP_DEBUG, PC104, COMPILE_TEST, USELIB, DEBUG_RSEQ` and the long list of `ARCH_<non-tegra>`. Marginal image-size + RAM savings; near-zero risk.
6. **Port `journald.conf` with `RuntimeMaxUse=64M`** (and `Storage=volatile` if we want to reduce UDA wear) via a tiny `systemd-conf_%.bbappend`.
7. **Evaluate `CONFIG_PREEMPT=y`** in a kernel fragment for audio latency (BT A2DP path). Test required.
8. **Port `mount.ignorelist`** for `/dev/nvme0n1*` + `/dev/mmcblk0*` etc. to prevent udev-extraconf misbehavior (if we ever add udev-extraconf to our image set).
9. **Bookmark** their `tegra-pinmux-dts2cfg-native` workdir-race serialization bbappend; copy if we ever see the race.

## Explicit non-actions

- Their full custom `tegra_defconfig` — drifts violently; keep our fragment approach.
- Mender, FIPS, Tailscale, samhain, integritycheck, otel — they're product features unrelated to our use.
- HDMI sysfs kernel patches (DDC power toggle, EDID reread) — they need HDMI; A203 is headless.
- BPMP powergate `GENPD_FLAG_ALWAYS_ON` for display — same: HDMI-specific.
- `tegra-storage-layout-base` UDA rewrite — Mender-specific.
- Their sshd FIPS-grade KEX/Ciphers — would erase our 1.65s login-latency win.
- Persistent-net rules with their MAC prefix.
- `NETWORKMANAGER_FIREWALL_DEFAULT = "iptables"` + `iptables` service — only relevant once we want a firewall.
