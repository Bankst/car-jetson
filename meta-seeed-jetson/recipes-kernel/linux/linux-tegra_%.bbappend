FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:jetson-xavier-nx-a203 = " \
    file://can.cfg \
    file://spi.cfg \
    file://usb-modem.cfg \
    file://usb-gadget.cfg \
"

# Replace kernel-built A203 DTBs with Seeed's prebuilts. tegra-bsp-a203 deploys
# the binaries to ${DEPLOY_DIR_IMAGE}/a203-dtb/; we copy them over the canonical
# names emitted by the kernel build at the deploy stage.
DEPENDS:append:jetson-xavier-nx-a203 = " tegra-bsp-a203"

do_deploy:append:jetson-xavier-nx-a203() {
    if [ -d "${DEPLOY_DIR_IMAGE}/a203-dtb" ]; then
        for f in ${DEPLOY_DIR_IMAGE}/a203-dtb/tegra194-p3668-*.dtb; do
            install -m 0644 "$f" "${DEPLOYDIR}/$(basename $f)"
        done
    fi
}
