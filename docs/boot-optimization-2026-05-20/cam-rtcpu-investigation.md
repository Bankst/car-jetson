# tegra186-cam-rtcpu disable — investigation + result

## Goal
Reclaim ~20s of kernel boot time spent loading t194-rce-safe Camera-FW.
No cameras on the project (A203 V2 / devkit headless).

## Result
SUCCESS. linux-tegra 5.10.216 + banks-jetson-image-base build cleanly
with all camera RTCPU / VI / sensor drivers disabled.

Artifacts (build/tmp/deploy/images/jetson-xavier-nx-banks-devkit/):
- Image-...-20260520180257.bin       (32M kernel)
- banks-jetson-image-base...20260520181000.tegraflash.tar.gz  (182M)

## Files changed
- meta-seeed-jetson/recipes-kernel/linux/linux-tegra/no-camera.cfg  (new)
- meta-seeed-jetson/recipes-kernel/linux/linux-tegra_%.bbappend     (SRC_URI append)

## CONFIG options disabled (final list)
Core RTCPU + IVC consumers:
- TEGRA_CAMERA_RTCPU            (was =y) — the actual culprit
- TEGRA_CAMERA_HSP_MBOX_CLIENT  (depends-on)
- I2C_TEGRA_CAMRTC              (depends-on)

VI / host1x camera capture:
- VIDEO_TEGRA_VI, VIDEO_TEGRA_VI_TPG
- TEGRA_CAMERA_PLATFORM
- TEGRA_GRHOST_NVCSI, TEGRA_GRHOST_SLVSEC, TEGRA_GRHOST_ISP

camera_common framework + sensors:
- VIDEO_CAMERA, VIDEO_CAMERA_SKT
- NV_VIDEO_IMX185/219/268/274/318/390/477, LC898212
- NV_VIDEO_OV5693/9281/10823/23850, AR0234, HAWK_OWL
- NV_DESER_MAX96712, NV_VIRTUAL_I2C_MUX
- VIDEO_TC358840, VIDEO_LT6911UXC, VIDEO_ECAM
- I2C_IOEXPANDER_SER_MAX9295, I2C_IOEXPANDER_DESER_MAX9296
- VIDEO_CDI, VIDEO_ISC

KEPT (intentional): TEGRA_GRHOST=y — display/NVDEC/NVENC/NVJPG/VIC/GPU.

## Iteration log
1. RTCPU+VI+NVCSI+SLVSEC+HSP+CAMRTC+CAMERA_PLATFORM+VI_TPG -> FAILED:
   linker errors in media/.../camera/vi/* (vb2_*, tegra_camera_*) and
   video/tegra/host/isp/isp5.c.
2. +VIDEO_CAMERA + GRHOST_ISP -> FAILED: nv_imx390.c (=y in defconfig)
   needs tegracam_*/regmap_util_*. IOEXPANDER_SER/DESER =y similarly.
3. + all NV_VIDEO_* + IOEXPANDERs + ECAM/TC358840/LT6911UXC -> FAILED in
   modules: cam_cdi_tsc.ko references Hawk_Owl_Fsync_program.
4. + VIDEO_CDI + VIDEO_ISC -> SUCCESS after `bitbake -c clean linux-tegra`.

## Initrd kernel
`meta-tegra/recipes-core/images/tegra-initrd-flash-initramfs.bb` is busybox
+ flash-tools only. The kernel `Image` for both normal boot and the
initrd-flash bundle comes from one linux-tegra recipe (single Image in
deploy/images/). The rebuild refreshed
`tegra-initrd-flash-initramfs-*.cpio.gz.cboot` alongside the new Image.

## Userspace risk assessment
- meta-seeed-jetson packagegroups: no argus/nvargus/libnvargus/
  tegra-libraries-camera references.
- No V4L2 capture apps in image.
- BT/audio/PipeWire unaffected.
- Risk: LOW. To revert, drop file://no-camera.cfg from bbappend.

## Expected boot-time saving
scripts/uart-20260520-132015.log shows kernel monotonic clock pausing from
~3.7s to ~24.5s waiting on:
  [   24.479718] Camera-FW on t194-rce-safe started
  [   24.555572] Camera-FW on t194-rce-safe ready ...
Estimated saving: 18-21s.

## Build commands
```
KAS_BUILD_DIR=$PWD/build KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
  kas-container \
    --runtime-args "--security-opt label=disable" \
    --runtime-args "--network=host" \
    --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
    shell kas/devkit.yml -c \
    "bitbake linux-tegra && bitbake banks-jetson-image-base"
```

## Flash
```
mkdir -p /tmp/flash
tar xzf build/tmp/deploy/images/jetson-xavier-nx-banks-devkit/\
banks-jetson-image-base-jetson-xavier-nx-banks-devkit.rootfs.tegraflash.tar.gz \
  -C /tmp/flash
cd /tmp/flash && sudo ./initrd-flash --erase-nvme
```
