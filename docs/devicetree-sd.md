# SD Card / DeviceTree

## How DTB reaches kernel
- UEFI (L4TLauncher / `bootaa64.efi`) does **not** load from `kernel-dtb` NVMe partition at runtime — MB2/firmware loads from QSPI, bypassing NVMe entirely.
- `kernel-dtb` and `kernel-dtb_b` NVMe partitions exist but are ignored by the running UEFI chain.
- **Fix**: `FDT` line in `/boot/extlinux/extlinux.conf` — L4TLauncher honors this and loads from rootfs.
- Baked via `UBOOT_EXTLINUX_FDT = "/boot/devicetree/tegra194-p3668-a203.dtb"` in `conf/machine/jetson-xavier-nx-a203.conf`.

## Custom DTB recipe
- `meta-seeed-jetson/recipes-bsp/seeed-a203-devicetree/` — `inherit devicetree`, compiles full DTB.
- Source: `tegra194-p3668-a203.dts` includes `tegra194-p3668-all-p3509-0000.dts` then `a203-sd.dtsi`.
- Installs to `/boot/devicetree/tegra194-p3668-a203.dtb` on rootfs.
- `PREFERRED_PROVIDER_virtual/dtb = "seeed-a203-devicetree"` in machine conf.

## SD card (sdhci@3440000 = mmc2)
- CD GPIO: PQ.02 (`&tegra_main_gpio 0x82`) — physically low when card inserted.
- Correct polarity: `cd-gpios = <&tegra_main_gpio 0x82 0x00>` (GPIO_ACTIVE_HIGH) + `cd-inverted;` from base DTB.
  - Base DTB already has `cd-inverted;` on sdhci@3440000 — this toggles gpiod active level = effective ACTIVE_LOW.
  - `GPIO_ACTIVE_LOW` (0x01) + `cd-inverted` = double inversion = **wrong** (don't do this).
- dmesg when working: `sdhci-tegra 3440000.sdhci: Got CD GPIO` then card enumerated as `mmc2` / `mmcblk2`.

## DTB deploy (no reflash needed)
```sh
# Build
kas-container (icecc) shell kas/base.yml -c "bitbake seeed-a203-devicetree"
# Push (reboot required — extlinux.conf read at boot by L4TLauncher)
jtx push seeed-a203-devicetree
```
