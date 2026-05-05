# Banks Jetson Linux

Yocto OE4T image for NVIDIA Jetson Xavier NX on a SeeedStudio A203 V2 carrier,
booting from eMMC with rootfs on external NVMe.

- Yocto: `scarthgap` (5.0 LTS)
- meta-tegra: `scarthgap-l4t-r35.x` → L4T R35.6.4 / JetPack 5.1.6
- Distro: `banks-jetson`
- Default image: `banks-jetson-image-plasma` (KDE Plasma 6 Wayland, minimal subset)

## Build

Requires Podman (or Docker) and the `kas-container` script.

```bash
# Base image (console only, no DE)
kas-container build kas/base.yml

# Plasma desktop image
kas-container build kas/plasma.yml
```

Artefacts land in `build/tmp/deploy/images/jetson-xavier-nx-a203/`.

## Flash

1. Plug a USB-A → micro-USB cable from your host to the A203 V2 micro-USB port.
2. Short the **FC REC** pin to **GND** on the A203 V2 (recovery mode).
3. Power on the board.
4. From `build/tmp/deploy/images/jetson-xavier-nx-a203/`, run the
   generated `initrd-flash` script. Rootfs goes to `/dev/nvme0n1p1`,
   bootloader stays on the QSPI/eMMC.

## Layout

```
kas/                                   kas-container build configs
  base.yml                             headless base image
  plasma.yml                           = base + Plasma DE
  lxqt.yml                             placeholder for future LXQt variant

meta-seeed-jetson/                     local Yocto layer
  conf/distro/banks-jetson.conf        DISTRO definition
  conf/machine/jetson-xavier-nx-a203.conf
  recipes-bsp/tegra-bsp-a203/          A203 DTBs + pinmux from Seeed's pack
  recipes-bsp/tegra-bootfiles/         pinmux substitution bbappend
  recipes-bsp/seeed-a203-iface/        CAN @ 500kbps, spidev, modules-load
  recipes-kernel/linux/                kernel config fragment (CAN/SPI/USB-modem)
  recipes-core/packagegroups/          base + per-DE packagegroups
  recipes-core/images/                 image recipes
```

## Decoupling

DE-specific bits live only in `packagegroup-de-*-minimal.bb` and the matching
`banks-jetson-image-<de>.bb`. The base image (`banks-jetson-image-base`) and the
distro conf are DE-agnostic. To add LXQt: add a `meta-lxqt`-style repo to
`kas/lxqt.yml`, write `packagegroup-de-lxqt-minimal.bb`, write
`banks-jetson-image-lxqt.bb`. No edits to base.

## Inputs

`_input/203_jp514.tar.gz` — Seeed's A203 driver pack for JP 5.1.4. We use only
the device trees and pinmux from this; everything else is ignored or replaced
by upstream.
