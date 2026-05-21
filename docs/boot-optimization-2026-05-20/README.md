# Boot Optimization Investigation — 2026-05-20

Multi-agent boot-time investigation on Jetson Xavier NX devkit. Goal: cut
power-on → login from ~38s baseline toward sub-15s.

Hardware reference: this session was driven on the devkit P3509 carrier
because A203 V2 has no early-boot UART exposed. Findings should be
re-validated on A203 before shipping.

## Investigation reports (read in this order)

1. **`build-comparison.md`** — diff of our build vs `mc2-yocto-os` reference.
   Identifies portable wins (`PcdPlatformBootTimeOut=0`, `console=tty0`
   removal, `journald.conf RuntimeMaxUse=64M`, kernel disables).

2. **`defconfig-diff-categorized.md`** — categorized kernel config diff
   against mc2's full `tegra_defconfig`. 148 option diffs; 63 ours-enabled
   that mc2 disables. Includes ready-to-drop `no-debug.cfg` fragment.

3. **`firstboot-comparison.md`** — mc2's `init-extra.d/` architecture for
   doing first-boot work AT FLASH TIME instead of every-boot from rootfs.
   Key pattern: scripts in `/init-extra.d/` run inside flashing initrd
   between partition writes and reboot. Recommendations R1 (UDA format)
   and R3 (EFI timeout) ported.

4. **`cam-rtcpu-investigation.md`** — disabling Tegra camera RTCPU + V4L2 +
   sensors. `no-camera.cfg`. ~20s save from killing Camera-FW load wait.

5. **`no-bloat-investigation.md`** — `no-bloat.cfg` for irrelevant PC/server
   class drivers (Intel/Huawei/Mellanox NICs, joysticks, InfiniBand, PC
   sound cards). ~60 disables.

6. **`uefi-splash-investigation.md`** — UEFI splash render loop in
   `PlatformBootManagerAfterConsole`. Patch suppresses `BootLogoEnableLogo`,
   `DisplaySystemAndHotkeyInformation`, `MemoryTest`. ~7s save.

7. **`optee-warning-investigation.md`** — 15× repeated "Test UEFI variable
   auth key" warnings during BDS first-boot variable refresh. ~9s on boot
   after flash. **Confirmed first-boot-only**, no recurring cost.

8. **`usb-gadget-churn-investigation.md`** — `banks-usb-gadget.service`
   was waiting on `basic.target` (kernel +12s) before firing. Moved to
   `sysinit.target` → fires at kernel +8.6s. **WARNING**: live override
   introduced a `sysinit.target` ordering cycle on
   `systemd-journal-catalog-update.service` — needs recipe-level fix.

9. **`edk2-investigation.md`** — debunked CLAUDE.md note #4. Source-built
   EDK2 already works; no GenFw error in scarthgap GCC 13. Original
   workaround (`tegra-uefi-prebuilt`) is stale; we already use source.

10. **`easy-fixes-investigation.md`** — implementation log for the
    three coordinated fixes (no-debug.cfg, NM-wait-online mask, init-extra
    migrations).

## Status as of session end

| Fix | Status |
|---|---|
| Camera RTCPU disable | ✓ shipped (in-image; UART confirmed Camera-FW gone) |
| UEFI splash patch (3 calls) | ✓ shipped (UART splash render dropped 11s → 4s) |
| `banks-jetson-iface` rename + CAN/SPI/gadget MACHINE_FEATURES | ✓ shipped |
| `efi-timeout` recipe hardening (re-check var byte every boot) | ✓ shipped |
| `no-debug.cfg` | ⚠ wired in bbappend; build broke with seq_file errors — needed `USB_XHCI_HCD=y` + `USB_XHCI_TEGRA=y` re-pin; build retry pending |
| `no-bloat.cfg` | ✓ wired |
| `NetworkManager-wait-online.service` mask | ⚠ in `banks_bake_unit_fixes()`; pending successful build |
| `banks-persist-setup` → `init-extra.d/50-format-uda.sh` | ⚠ recipe written; pending successful build |
| `efi-timeout.service` → `init-extra.d/30-banks-efi-timeout.sh` | ⚠ recipe written; pending successful build |
| `banks-usb-gadget.service` ordering | ⚠ live override applied; creates new
   sysinit.target ordering cycle; recipe change pending |

## Open follow-ups

- Build that includes no-debug.cfg + NM-wait-online mask + init-extra
  migrations still failing as of session end. Resolve the `seq_file`
  compile errors first.
- `tegra-xudc` ordering-cycle on gadget service needs a cleaner unit
  recipe (probably `WantedBy=basic.target` with `After=local-fs.target`,
  not sysinit.target).
- `pcie@14160000` = M.2 Key E (Wi-Fi). 1.1s timeout per boot. User decision:
  disable in DTB (no Wi-Fi forever) vs keep for optional Wi-Fi vs investigate
  `pcie-deassert-delay-msec` shortening.
- A203 has no early-boot UART — all pre-kernel timing reported here is from
  devkit. Re-confirm gains on A203 hardware.

## UART log snapshots

Reference points in `scripts/uart-*.log`:
- `uart-20260520-114026.log` — baseline pre-tweaks (~38s)
- `uart-20260520-132015.log` — first devkit flash (UEFI patch baked, camera
  disabled) — ~38-40s, but timeline gives the per-phase ground truth
- `uart-20260520-144623.log` — post-flash boot #1 (EFI vars freshly wiped,
  ~66s due to OP-TEE warning loop + Timeout reset)
- `uart-20260520-150426.log` — post-flash boot #2 (~54s — OP-TEE loop
  drops to 1 warning, confirming first-boot-only)
- `uart-20260520-152448.log` — post-live-gadget-override boot (~53s; gadget
  EP 0 enable moves to kernel +8.6s, but new ordering cycle eats gains)
