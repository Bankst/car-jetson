# Easy fixes — investigation & implementation notes

## Fix 1: no-debug.cfg

- Created `meta-seeed-jetson/recipes-kernel/linux/linux-tegra/no-debug.cfg`
  with the sample fragment verbatim from `/tmp/defconfig-diff-categorized.md`.
  Covers ~28 disables: FTRACE family, SCHEDSTATS, SCHED_DEBUG, LOCKUP_DETECTOR
  family, KALLSYMS_ALL, FRAME_POINTER, MAGIC_SYSRQ, RCU_TRACE, etc.
  Sets `CRYPTO_MANAGER_DISABLE_TESTS=y` to skip crypto self-tests at boot.
  Keeps DEBUG_FS=y (NVIDIA tegra debug nodes) and base KALLSYMS=y (oops).
- Wired into `linux-tegra_%.bbappend` SRC_URI alongside existing
  no-camera.cfg / no-bloat.cfg.
- Expected save: 50–150 ms kernel init + ~1 MB Image shrink.

## Fix 2: NetworkManager-wait-online mask

- Added a `ln -sf /dev/null` symlink for
  `NetworkManager-wait-online.service` to
  `banks_bake_unit_fixes()` in
  `meta-seeed-jetson/recipes-core/images/banks-jetson-image-base.bb`.
  Mirrors the existing pattern used for systemd-networkd.
- Expected save: ~5.7 s userspace boot (blocks network-online.target).

## Fix 3: init-extra.d migrations (R1 + R3 from /tmp/firstboot-comparison.md)

### Discovery

- meta-tegra ships `tegra-flash-init` at
  `meta-tegra/recipes-core/initrdscripts/tegra-flash-init_1.0.bb`. The
  recipe creates `/init-extra.d/` and ships `init-extra` runner that
  loops over every file in that directory (no manifest gating — simpler
  than the OXOS MC2 fork).
- This means adding a script is as simple as a bbappend that drops a
  file into `${D}/init-extra.d/`. Two bbappends from `meta-seeed-jetson`
  cleanly compose (BBFILES glob already covers
  `recipes-*/*/*.bbappend`).

### R1 — UDA format moved to flash time

- New script `meta-seeed-jetson/recipes-core/banks-persist/files/50-format-uda.sh`:
  re-reads partition table, finds UDA by partlabel, mkfs.ext4 if not
  already ext4, mounts at /tmp/uda-mnt, seeds bluetooth/ssh/config
  dirs + stamp, unmounts.
- New bbappend `recipes-core/banks-persist/tegra-flash-init_%.bbappend`
  installs the script as `/init-extra.d/50-format-uda.sh`.
- `banks-persist_1.0.bb` rewritten:
  - Drops `banks-persist-setup` + `banks-persist-setup.service`.
  - Ships `data.mount` (systemd .mount unit, name matches /data path
    so systemd auto-generates the mount-as-target wiring) and the
    existing `banks-persist-bind` service.
  - SYSTEMD_SERVICE now `data.mount banks-persist-bind.service`.
- `files/data.mount` updated to drop the now-removed
  `banks-persist-setup.service` After=/Requires=, kept BindsTo on the
  partlabel device unit.
- `files/banks-persist-bind.service` updated: After/Requires
  `data.mount` instead of `banks-persist-setup.service`.
- Removed obsolete `files/banks-persist-setup` and
  `files/banks-persist-setup.service`.

### R3 — efi-timeout moved to flash time

- New script
  `meta-seeed-jetson/recipes-bsp/efi-timeout/files/30-efi-timeout.sh`:
  mounts efivarfs if not already, drops immutable flag, writes
  `0x07,0x00,0x00,0x00,0x00,0x00` (NV|BS|RT attrs + UINT16 0) to the
  Timeout EFI var GUID. QSPI-resident → survives reflash.
- New bbappend
  `recipes-bsp/efi-timeout/tegra-flash-init_%.bbappend` installs as
  `/init-extra.d/30-efi-timeout.sh`.
- `efi-timeout_1.0.bb`: `SYSTEMD_AUTO_ENABLE = "disable"`. Service still
  ships, just not started at boot — manual `systemctl start
  efi-timeout` is a backstop.

## Build

- Kicked single devkit image build:
  `KAS_BUILD_DIR=$PWD/build KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
   kas-container --runtime-args "--security-opt label=disable" \
   --runtime-args "--network=host" --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
   build kas/devkit.yml`
- Log at `/tmp/easy-fixes-build.log`.

## Combined expected save

- Kernel init: 50–150 ms (no-debug.cfg)
- NM-wait-online unblock: ~5.7 s
- banks-persist-setup eliminated: 1–3 s (per R1 estimate)
- efi-timeout.service eliminated: 0.3–0.8 s (per R3 estimate)
- **Total: ~7–10 s off every boot**, plus ~1 MB kernel Image shrink.
