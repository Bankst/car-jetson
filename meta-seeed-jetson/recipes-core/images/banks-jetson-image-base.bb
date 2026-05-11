SUMMARY = "Banks Jetson Linux — base image (no DE)"
DESCRIPTION = "Headless console image for Xavier NX on Seeed A203 V2. \
Includes NVIDIA Tegra userspace, NetworkManager, audio, base CLI tools, \
A203 interface bring-up. No display server / DE — see banks-jetson-image-plasma."
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-openssh debug-tweaks package-management"

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    packagegroup-seeed-base \
"

IMAGE_LINGUAS = ""

ROOTFS_POSTPROCESS_COMMAND:append = " banks_trim_rootfs;"

banks_trim_rootfs() {
    for kdir in ${IMAGE_ROOTFS}${nonarch_base_libdir}/modules/*/kernel; do
        [ -d "$kdir" ] || continue
        rm -rf $kdir/drivers/infiniband
        rm -rf $kdir/drivers/media
        rm -rf $kdir/drivers/net/ethernet
        rm -rf $kdir/drivers/net/wireless/realtek
        rm -rf $kdir/drivers/net/wireless/intel
        rm -rf $kdir/fs/btrfs
        rm -rf $kdir/fs/cifs
        rm -rf $kdir/fs/nfsd
    done

    rm -rf ${IMAGE_ROOTFS}${nonarch_base_libdir}/udev/hwdb.bin
    rm -rf ${IMAGE_ROOTFS}${nonarch_base_libdir}/udev/hwdb.d

    rm -f ${IMAGE_ROOTFS}${bindir}/rsaperf
    rm -f ${IMAGE_ROOTFS}${bindir}/pk11ectest
    rm -f ${IMAGE_ROOTFS}${bindir}/fipstest
    rm -f ${IMAGE_ROOTFS}${bindir}/bltest
    rm -f ${IMAGE_ROOTFS}${bindir}/ecperf

    rm -rf ${IMAGE_ROOTFS}${datadir}/keymaps
    rm -rf ${IMAGE_ROOTFS}${datadir}/consolefonts
    rm -rf ${IMAGE_ROOTFS}${datadir}/sounds
}
