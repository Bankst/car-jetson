# no-bloat.cfg — trim PC-class drivers from Xavier NX kernel

## Goal
Remove driver classes that have ZERO chance of being relevant on this
hardware (Tegra T194, A203/devkit, headless). Each saves probe spam at boot
and trims rodata.

## Approach
Same shape as no-camera.cfg — single fragment, `# CONFIG_X is not set`
form, appended to SRC_URI in linux-tegra_%.bbappend.

## CONFIG options disabled (~60)

### PCI server / PC Ethernet (the big one — about 40 options)
Intel: E100 E1000 E1000E IGB IGBVF IXGB IXGBE IXGBEVF I40E I40EVF ICE
Huawei: HNS_DSAF HNS_ENET HNS3 HNS3_ENET HINIC
Broadcom: TIGON3 BNX2X BNXT CNIC
Mellanox: MLX4_EN MLX4_INFINIBAND MLX5_CORE MLX5_CORE_EN MLX5_CORE_IPOIB
         MLX5_CORE_THERMAL MLX5_INFINIBAND MLXSW_CORE MLX_MFT
Chelsio: CHELSIO_T1 CHELSIO_T1_1G CHELSIO_T3 CHELSIO_T4 CHELSIO_T4VF
QLogic: QLA3XXX QLCNIC NETXEN_NIC
Others: S2IO VXGE FORCEDETH SFC ENIC THUNDER_NIC_PF THUNDER_NIC_VF
        LIQUIDIO LIQUIDIO_VF ATL1 ATL1C ATL1E ATL2 ALX SKGE SKY2
        SKY2_DEBUG 8139CP 8139TOO TYPHOON DL2K JME SXGBE_ETH

### InfiniBand
INFINIBAND INFINIBAND_USER_MAD INFINIBAND_USER_ACCESS INFINIBAND_MTHCA
INFINIBAND_IPOIB INFINIBAND_IPOIB_CM INFINIBAND_SRP

### PC sound
SND_HDA_INTEL (PCI Intel HDA — Tegra HDMI uses SND_HDA_TEGRA, kept)

### Joystick / gamepad
INPUT_JOYDEV JOYSTICK_XPAD

### Other
ATMEL (legacy PCI WiFi chipset)

## What was deliberately KEPT (don't be tempted)
- SND_HDA_TEGRA + SND_HDA_CODEC_HDMI (HDMI audio path on Xavier NX)
- SND_USB_AUDIO (USB headset / Sennheiser BT bridge)
- R8168 (A203 PCIe NIC), USB_RTL8152 (USB GigE), NVETHERNET (Tegra MAC)
- All RTL* Wi-Fi chipsets (M.2 Key E module is user-replaceable)
- KEYBOARD_ATKBD MOUSE_PS2 SERIO_SERPORT (touched-not-removed; NanoKVM may
  reuse the serio stack for emulated PS/2 paths — risk-averse)
- SMSC911X — kept until verified nothing wants it
- ACPI subsystem entirely

## High-value =y disables (built-in, not modules)
TIGON3, CHELSIO_T1_1G, HNS_DSAF, HNS_ENET, HNS3, HNS3_ENET, E1000E, IGB,
MLX5_CORE_EN, MLX5_CORE_IPOIB, MLX5_CORE_THERMAL, MLXSW_CORE, FORCEDETH,
INFINIBAND_IPOIB_CM, INPUT_JOYDEV, JOYSTICK_XPAD — these reduce the kernel
Image itself, not just modules.

## Files changed
- meta-seeed-jetson/recipes-kernel/linux/linux-tegra/no-bloat.cfg  (new)
- meta-seeed-jetson/recipes-kernel/linux/linux-tegra_%.bbappend    (SRC_URI append)

## Build command
KAS_BUILD_DIR=$PWD/build KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
  kas-container --runtime-args "--security-opt label=disable" \
  --runtime-args "--network=host" --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
  shell kas/devkit.yml -c "bitbake -c kernel_configme -f linux-tegra && \
                          bitbake banks-jetson-image-base"

## Expected effect
- Probably 30+ fewer "module not found / device not present" probe attempts
  during udev coldplug. Most of these chipsets aren't even present on the
  PCIe bus, so probe is just a quick negative — but it adds up.
- Smaller kernel Image (the =y ones save real space).
- Negligible boot-time win compared to camera RTCPU (which was a 20s
  outright stall). Maybe 100–500ms udev settle saving.
