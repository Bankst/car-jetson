# Kernel defconfig diff: mc2-yocto-os vs car-jetson (ours)

Read-only analytical report. Other agent `a205458e323e0cf1d` has already
shipped `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/no-bloat.cfg`
covering ~60 server/PC driver disables (NICs, InfiniBand, HDA Intel,
joystick). This report covers **what's left** — the high-value disables
mc2 makes that we do not, focused on actual boot-time and kernel-size
impact.

## Sources

| File | What it is | Size |
|---|---|---|
| Baseline | `build/tmp/work-shared/jetson-xavier-nx-a203/kernel-source/arch/arm64/configs/tegra_defconfig` (meta-tegra's `tegra_defconfig` shipped with linux-tegra 5.10.216) | 1397 lines |
| mc2 (reference) | `mcx-monorepo/.../meta-oxos-mc2/recipes-kernel/linux/custom/tegra_defconfig` | 8613 lines (full-resolved override) |
| Ours (resolved) | `build/tmp/work/.../linux-tegra/.../linux-...-build/.config` | 8554 lines |

Total differing options after normalizing: **148** (out of ~7000 total
symbols both configs touch). Of those, **63** are options mc2 disables
that we ship enabled (`=y` or `=m`); the other ~85 are minor mode
differences, mc2-specific FIPS/MHI/ATH11K enables, and string/version
deltas (compiler version, LOCALVERSION).

## Executive summary

- **Biggest unexploited win: the tracing/debug stack.** mc2 disables 28
  options across `TRACING`, `FTRACE`, `KALLSYMS_ALL`, `SCHED_DEBUG`,
  `LOCKUP_DETECTOR`, `DEBUG_INFO`, `DEBUG_KERNEL`, `MAGIC_SYSRQ`, etc.
  We have them all on. This is the single largest source of kernel-image
  bloat and per-tracepoint init cost that's not yet addressed by
  `no-bloat.cfg`.
- **`no-bloat.cfg` already covers the noisy stuff** (server NICs,
  InfiniBand, HDA Intel, joystick) — 60 options. The remaining ~60 from
  the diff are non-driver kernel infrastructure: tracing, debug, crypto
  algorithms.
- **No driver-init lines for the no-bloat.cfg disables appear in the
  UART boot log.** The PCI NIC drivers never probed in the first place
  (nothing on the bus). Removal is a kernel-Image-size + module-count
  win, not a measurable wall-clock dmesg win.
- **Real boot-time waste in the actual UART log is dominated by
  non-kernel-config issues**: a 2.4s + 7.2s + 18s sequence of
  `tegra-xudc` USB gadget enable/disable cycles between kernel ts 9-37s.
  No defconfig change will move that needle.
- **DON'T port from mc2 blindly**: `CONFIG_HOTPLUG_CPU=n` (mc2) breaks
  Tegra CPU idle and would prevent the 6-core SMP from suspending cores;
  `MODULE_SIG=y` requires signing infrastructure we don't have;
  `OF_OVERLAY` *we need* (DT overlay support is `=y` in our
  `dt-overlay.cfg`).

## Diff classification (mc2 → ours)

| mc2 → ours | Count | Meaning |
|---|---:|---|
| `n` → `y` | 41 | disabled in mc2, builtin in ours — disable candidates |
| `n` → `m` | 22 | disabled in mc2, module in ours — disable candidates |
| `n` → `_` | 37 | both effectively off (mc2 explicit, ours implicit via dependency) |
| `y` → `_`  | 11 | mc2 enables, ours auto-omits (e.g. FRAMOS, MAX96792 — camera ICs) |
| `m` → `_`  | 10 | mc2 module, ours absent (ATH11K_PCI, IMX_SDMA, QRTR_MHI, IMX cameras) |
| `m` → `n`  |  7 | mc2 module, ours explicit-off (mostly camera + Wi-Fi pieces) |
| `y` → `n`  |  5 | mc2 builtin, ours explicit-off (MODULE_SIG, FB_CONSOLE_DEFERRED_TAKEOVER, GPIO_MAX732X, HW_RANDOM_CCTRNG) |
| `y` → `m`  |  4 | mc2 builtin, ours module (CRYPTO_ANSI_CPRNG, HW_RANDOM, OVERLAY_FS, USB_NET_CDC_NCM) |
| `_` → `y`  |  2 | USB_F_EEM, USB_F_SUBSET — our usb-gadget.cfg additions |
| strings    |  6 | compiler version, LOCALVERSION, rtc0→rtc1 default, no real-world effect |

## Category-by-category breakdown

### 1. Kernel infrastructure debt (28 options) — biggest unexploited win

mc2 strips the entire ftrace / debug-info / lockup-detector stack.
Effect: smaller kernel Image (debug-info alone is 10-30 MB at compile
time, but `INSTALL_MOD_STRIP=1` in OE strips most of it from the
deployed `Image`; ~1-2 MB of vmlinux .text), zero per-tracepoint init
cost, fewer locks taken on hot paths.

| Option | ours | mc2 | UART evidence | Estimate |
|---|---|---|---|---|
| `CONFIG_FTRACE` / `FUNCTION_TRACER` / `FUNCTION_GRAPH_TRACER` / `STACK_TRACER` / `GENERIC_TRACER` / `EVENT_TRACING` / `TRACING` / `TRACING_SUPPORT` / `RCU_TRACE` / `TRACEPOINTS` | y | n | silent (init is fast but pervasive) | 30–80 ms kernel init + ~600 KB Image |
| `KALLSYMS_ALL` / `KALLSYMS_BASE_RELATIVE` (KALLSYMS itself is selected by FTRACE/PRINTK so it stays) | y | n | silent | ~200 KB Image (kallsyms table) |
| `DEBUG_INFO` / `DEBUG_KERNEL` / `DEBUG_FS` / `DEBUG_FS_ALLOW_ALL` | y | n | silent | Build time + dev-tool surface area; minimal runtime |
| `SCHEDSTATS` / `SCHED_DEBUG` / `SCHED_INFO` | y | n | silent | per-context-switch counters; 5–15 ns hot-path saving × millions of switches |
| `LOCKUP_DETECTOR` / `SOFTLOCKUP_DETECTOR` / `DETECT_HUNG_TASK` / `WQ_WATCHDOG` | y | n | silent | 1 watchdog timer per CPU; ~10 ms timer setup + ongoing |
| `MAGIC_SYSRQ` / `MAGIC_SYSRQ_SERIAL` | y | n | silent | tiny |
| `FRAME_POINTER` | y | n | silent | -fno-omit-frame-pointer adds 1-3% .text size + 2-5% perf on call-heavy code |
| `STACKTRACE` / `LOCK_DEBUGGING_SUPPORT` / `HAVE_DEBUG_BUGVERBOSE` | y | n | silent | minor |
| `HAVE_ARCH_KGDB` / `HAVE_KCSAN_COMPILER` | y | n | silent | feature-flags only; compile-time |
| `ARCH_HAS_UBSAN_SANITIZE_ALL` | y | n | silent | UBSAN runtime hooks if enabled; effectively no-op without `UBSAN=y` |
| `HOTPLUG_CPU` | y | n | **keep ours**: required for cpuidle on Tegra | — |

**Recommendation**: extend `no-bloat.cfg` with the tracing/debug stack
**except** `HOTPLUG_CPU`, `KALLSYMS` (selected automatically by
`PRINTK`+arm64), and `DEBUG_FS` (used by NVIDIA's tegra debug nodes —
see `tegra_powergate_debugfs_init` etc. in the kernel; turning DEBUG_FS
off may break diagnostic interfaces nvgpu/nvhost expose). Best to keep
DEBUG_FS=y but disable the heavy users (FTRACE/SCHED_DEBUG).

**Estimated cumulative savings**: 50–150 ms kernel init, ~1 MB Image
reduction (after compression, ~200-400 KB on disk), measurable cache
hit-rate improvement on hot scheduler paths. Not a single dramatic
saving — death by a thousand cuts the other way.

### 2. Crypto algorithms (~15 options) — silent disable

mc2 disables many crypto algorithm modules: `CRYPTO_DES`, `MD4`, `MD5`
(!), `SHA3`, `SHA3_ARM64`, `SM3`, `SM4`, `TWOFISH`, `BLAKE2B`,
`MICHAEL_MIC`, `LZO`, `XXHASH`, `ARC4`, `LIB_ARC4`, `LIB_DES`,
`MANAGER_DISABLE_TESTS`. Also the ARM64 hardware-accelerated variants
`AES_ARM64_NEON_BLK`, `GHASH_ARM64_CE`, `SHA1_ARM64_CE`,
`SM3_ARM64_CE`.

**Caution**: WPA2/Wi-Fi needs `MICHAEL_MIC` (TKIP) and several Wi-Fi/BT
stacks pull in `ARC4`/`MD5`. dm-verity uses `SHA256` (kept by default).

| Subcat | UART evidence | Estimate |
|---|---|---|
| Crypto self-tests (`CRYPTO_MANAGER_DISABLE_TESTS=y` in mc2) | silent in current log (already running tests; cost is during crypto module init) | 20–60 ms saved at boot per architecture-specific algorithm registration |
| Removing rarely-used algos | silent | ~50–200 KB modules off rootfs |

**ARM64 CE accelerators**: leaving `SHA1_ARM64_CE`, `GHASH_ARM64_CE`,
`AES_ARM64_NEON_BLK` as `=m` is fine; they're loaded on demand. mc2
disables them entirely (probably no Wi-Fi WPA needed there) — we have
Wi-Fi, so **don't disable these**.

**Recommendation**: set `CRYPTO_MANAGER_DISABLE_TESTS=y` (skip
self-tests at boot — 20-60 ms). Leave the rest alone unless you can
confirm dm-crypt/Wi-Fi don't pull them.

### 3. SCSI/iSCSI subsystem

mc2 has `CONFIG_SCSI=n` (and presumably the whole subsystem off). Ours
ships `SCSI=y` and a ton of SCSI-related options.

| Option | ours | mc2 | Notes |
|---|---|---|---|
| `CONFIG_SCSI` | y | n | NVMe doesn't need SCSI; USB-storage *does* (uses `usb-storage` → `scsi-mod`) |
| `CONFIG_BLK_DEV_SD` etc. | y | n (cascaded) | likewise |

**USB mass-storage requires SCSI.** If you ever plug a USB stick to the
Jetson, the kernel uses the `usb-storage` driver which goes through the
SCSI layer. Unless USB storage is explicitly not in scope, **keep SCSI
enabled**. mc2 may not need it.

Silent disable in UART log. **No boot-time win.** Skip.

### 4. USB CONFIGFS gadget subset

`CONFIG_USB_CONFIGFS_ECM_SUBSET=y`, `USB_CONFIGFS_EEM=y` — mc2 disables
both. We enable both intentionally in `usb-gadget.cfg`. NCM is what
we use; EEM/ECM subset are alternative gadget functions Banks's usb
gadget script doesn't currently configure but the script is `configfs`-
based and can be reconfigured at runtime. **Keep both.** No boot impact
(gadget init happens in userspace from `banks-usb-gadget.service`).

### 5. NVIDIA cameras `IMX219`, `IMX477` (=m → mc2 disabled)

These two stayed enabled in our config despite `no-camera.cfg`. They're
modules and don't auto-load (no DTS node binds them), but they sit in
the module tree taking disk space and modules.builtin/modules.dep
lookup time.

**Recommendation**: add to `no-camera.cfg`. Already aligned with our
"no cameras" stance.

### 6. Should we port FROM mc2 (feature parity candidates)

| mc2 has, we lack | Why mc2 wants it | Should we? |
|---|---|---|
| `CONFIG_HW_RANDOM_TPM=y` | TPM 2.0 hardware RNG entropy source | only if we add TPM hardware — A203 has none |
| `CONFIG_HW_RANDOM_CCTRNG=y` | ARM CC TRNG IP | not present in T194 — skip |
| `CONFIG_CRYPTO_ANSI_CPRNG=y` (we have =m) | userspace random | already covered by `getrandom(2)` + crng |
| `CONFIG_MODULE_SIG=y`, `MODULE_SIG_SHA256`, `MODULE_SIG_ALL` | signed-module enforcement | requires signing key infrastructure; security-vs-dev tradeoff |
| `CONFIG_CRYPTO_FIPS=y` | FIPS 140-2 compliance | medical device requirement at OXOS — not us |
| `CONFIG_ATH11K`/`ATH11K_PCI` (=m) | Qualcomm Wi-Fi 6 chipset | only if your M.2 Key E module ships Atheros 11k — currently we use Realtek; skip |
| `CONFIG_MHI_BUS` | Modem Host Interface (Qualcomm modems) | tied to ATH11K/QRTR — skip |
| `CONFIG_NF_TABLES` (=m), `NETFILTER_XT_MATCH_DSCP`, `NETFILTER_XT_TARGET_DSCP` | nftables firewall | useful for production but ours is dev-stage; consider when we lock down |
| `CONFIG_GPIO_MAX732X` (=y) | MAX7320/7321 I/O expander | only if we add one |
| `CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER=y` | smoother boot splash (delays fbcon takeover until first message) | **maybe** — could eliminate the brief tty1 flash before kiosk weston grabs the framebuffer. Cheap to enable. |
| `CONFIG_FRAMEBUFFER_CONSOLE_ROTATION=y` | rotated console | no — we don't rotate |
| `CONFIG_TOUCHSCREEN_EDT_FT5X06` (=m) | FocalTech touchscreen | only if you add this panel |

### 7. =y vs =m mode differences

| Option | mc2 | ours | Comment |
|---|---|---|---|
| `CONFIG_HW_RANDOM` | y | m | builtin = ready before initramfs userspace; module = waits for udev. Boot-time relevant. Worth setting `=y` (~5-15 ms earlier seed). |
| `CONFIG_OVERLAY_FS` | y | m | We use this in kiosk + rootfs overlay. Builtin avoids module-load step in initrd. Worth `=y`. |
| `CONFIG_USB_NET_CDC_NCM` | y | m | NM brings up usb0 bridge at userspace; module load is fast and triggered by gadget enumeration. Either is fine; `=y` slightly earlier. |

## Top 10 specific disables by estimated boot-time saved

Ranked by combined (kernel init time + Image size reduction + indirect
cache benefit). All of these are options **not yet** in agent
`a205458e323e0cf1d`'s `no-bloat.cfg`.

| Rank | Option | Estimate | Risk |
|---|---|---|---|
| 1 | `CONFIG_FTRACE` (+ FUNCTION_TRACER, FUNCTION_GRAPH_TRACER, STACK_TRACER, GENERIC_TRACER, EVENT_TRACING, TRACING) | 30–80 ms init + ~600 KB Image | low — no production code uses ftrace |
| 2 | `CONFIG_SCHEDSTATS` + `SCHED_DEBUG` + `SCHED_INFO` | 5–15 ns per context-switch × N | low |
| 3 | `CONFIG_LOCKUP_DETECTOR` + `SOFTLOCKUP_DETECTOR` + `DETECT_HUNG_TASK` + `WQ_WATCHDOG` | 5–20 ms init + ongoing timers | low (you lose watchdog kernel taint diagnostics) |
| 4 | `CONFIG_CRYPTO_MANAGER_DISABLE_TESTS=y` | 20–60 ms (skips alg self-test in `tcrypt`) | low |
| 5 | `CONFIG_KALLSYMS_ALL` (keep KALLSYMS for oops decoding) | 100–200 KB Image | low |
| 6 | `CONFIG_FRAME_POINTER` | 1-3% .text size, marginal hot-path perf | very low (only affects unwinding accuracy when no ORC) |
| 7 | `CONFIG_DEBUG_INFO` | huge build-time saving; deployed Image already strips most | none at runtime |
| 8 | `CONFIG_RCU_TRACE` + `TRACEPOINTS` | 10–30 ms | low |
| 9 | `CONFIG_NV_VIDEO_IMX219` + `IMX477` (move to `no-camera.cfg`) | ~80 KB modules + udev probe blip | none — already excluded |
| 10 | `CONFIG_MAGIC_SYSRQ` + `MAGIC_SYSRQ_SERIAL` | ~10 KB; loses Alt-SysRq for dev debug | medium — useful when serial console wedges |

**What's NOT here, intentionally**: `HOTPLUG_CPU` (needed for cpuidle),
`OF_OVERLAY` (we use overlays via `dt-overlay.cfg`), `SCSI` (USB
storage uses it), `DEBUG_FS` (NVIDIA drivers create debug nodes there),
the ARM64-CE crypto accelerators (Wi-Fi WPA needs them), most crypto
modules (Wi-Fi/Bluetooth pull them).

## Sample patch fragment — `no-debug.cfg`

Drop into `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/` and
append to `SRC_URI` in `linux-tegra_%.bbappend`.

```ini
# no-debug.cfg — strip tracing and debug infrastructure
# Mirrors mc2's defconfig stance: no ftrace, no SCHED_DEBUG, no watchdogs.
# Saves ~600 KB of vmlinux .text and 30-150 ms of kernel init.
# Keeps DEBUG_FS=y (NVIDIA drivers expose debug nodes there) and
# KALLSYMS=y (oops symbol resolution).

# Function tracing
# CONFIG_FTRACE is not set
# CONFIG_FUNCTION_TRACER is not set
# CONFIG_FUNCTION_GRAPH_TRACER is not set
# CONFIG_STACK_TRACER is not set
# CONFIG_GENERIC_TRACER is not set
# CONFIG_EVENT_TRACING is not set
# CONFIG_TRACING is not set
# CONFIG_TRACEPOINTS is not set
# CONFIG_RCU_TRACE is not set

# Scheduler debug / stats
# CONFIG_SCHEDSTATS is not set
# CONFIG_SCHED_DEBUG is not set
# CONFIG_SCHED_INFO is not set

# Hung-task / softlockup watchdogs
# CONFIG_LOCKUP_DETECTOR is not set
# CONFIG_SOFTLOCKUP_DETECTOR is not set
# CONFIG_DETECT_HUNG_TASK is not set
# CONFIG_WQ_WATCHDOG is not set

# Frame pointer / kallsyms-all (keep KALLSYMS for oops)
# CONFIG_FRAME_POINTER is not set
# CONFIG_KALLSYMS_ALL is not set

# Crypto: skip self-tests at boot
CONFIG_CRYPTO_MANAGER_DISABLE_TESTS=y

# Magic SysRq (lose Alt-SysRq from serial — keep DEBUG_KERNEL though)
# CONFIG_MAGIC_SYSRQ is not set
# CONFIG_MAGIC_SYSRQ_SERIAL is not set

# Verbose BUG() output
# CONFIG_HAVE_DEBUG_BUGVERBOSE is not set
```

And extend `no-camera.cfg` with the two stragglers:

```ini
# CONFIG_NV_VIDEO_IMX219 is not set
# CONFIG_NV_VIDEO_IMX477 is not set
```

## Where mc2 diverges from us in ways NOT to copy

1. **`# CONFIG_HOTPLUG_CPU is not set`** — would prevent
   secondary-CPU bring-down for cpuidle. Carmel six-core idle savings
   would be lost.
2. **`# CONFIG_SCSI is not set`** — breaks USB mass storage. mc2's
   target apparently doesn't use it; ours does.
3. **`# CONFIG_OF_OVERLAY is not set`** — we use device-tree overlays
   via `dt-overlay.cfg`. Disabling breaks that.
4. **`# CONFIG_KALLSYMS is not set`** — implied via the diff; but kernel
   panics print "(no symbol)" without it. Keep KALLSYMS, just drop
   `KALLSYMS_ALL`.
5. **`# CONFIG_CRYPTO_*_ARM64_CE is not set`** — losing AES-NI-like
   hardware crypto on aarch64 hurts dm-crypt + Wi-Fi throughput. We
   want these as modules at minimum.
6. **`CONFIG_MODULE_SIG=y`** — requires a signing key in the build
   pipeline. We don't have that infra; would silently sign modules with
   an ephemeral key and become un-loadable on reboot.

## What the UART log actually says about boot cost

Kernel boot from `[ 0.000000]` to last useful line (`vdd-fan: disabling`)
spans **37.86 s of kernel wall time**.

Big gaps (where the time actually goes):

| ts (kernel) | gap   | Cause | Defconfig fix? |
|---:|---:|---|---|
|  2.234 s | +1.24 s | `printk: console [ttyTCU0] enabled` — serial console buffered output flush | no — drop `loglevel=7` in cmdline or `quiet` boot |
|  4.973 s | +1.12 s | `tegra194-pcie 14160000.pcie: Phy link never came up` (PCIe slot empty) | partial — could disable that controller in DT |
|  9.827 s | +2.42 s | `tegra-xudc: ep 3 disabled` (USB gadget cycle 1/3) | no — userspace gadget reconfiguration |
| 18.403 s | +7.16 s | tegra-xudc cycle 2 | no |
| 37.860 s | +18.16 s | tegra-xudc cycle 3 → regulator-disable storm | no |

**Conclusion**: defconfig-based wins target the first ~4 seconds of
kernel init (memory setup, driver registration, device probe). The
biggest **gaps** in our log are USB-gadget bring-up driven from
userspace — `banks-usb-gadget.service` — and are not addressable here.
Expect cumulative **50–200 ms** of kernel boot time saved from a fully
applied `no-bloat.cfg` + `no-debug.cfg`. Much more meaningful: 1-2 MB
of Image+module-tree shrinkage and reduced udev coldplug surface.

## Coordination

- `a205458e323e0cf1d` (kernel bloat trim) — owns `no-bloat.cfg`,
  PCI/server driver disables. No overlap with this report's
  recommendations.
- Next step (if Banks wants to ship): create `no-debug.cfg` per the
  fragment above, append `SRC_URI` in `linux-tegra_%.bbappend`,
  rebuild kernel, reflash. Or fold these into `no-bloat.cfg`.

## Skipped (per task scope)

- mc2's FIPS / module-sig / Mender deltas — not relevant to us.
- Compiler-version and LOCALVERSION strings.
- Symbols selected by other symbols (e.g. `KALLSYMS` selected by
  `BPF_SYSCALL`; touching them upstream is brittle).
- `TRACING_SUPPORT` is a kconfig prompt-less helper auto-selected by
  arch; cannot be disabled directly.
