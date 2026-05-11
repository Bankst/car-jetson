SEEED_PINMUX_DIR := "${THISDIR}/tegra-bootfiles"

do_install:append:jetson-xavier-nx-a203() {
    cp ${SEEED_PINMUX_DIR}/tegra19x-mb1-pinmux-p3668-a01.cfg \
        ${D}/usr/share/tegraflash/tegra19x-mb1-pinmux-p3668-a01.cfg
}
