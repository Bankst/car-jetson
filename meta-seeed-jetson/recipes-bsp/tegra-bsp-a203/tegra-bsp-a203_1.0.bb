SUMMARY = "Seeed A203 V2 carrier board support for Xavier NX"
DESCRIPTION = "DT overlay enabling UARTC (debug serial) and pinmux config \
for the A203 V2 carrier. Uses the kernel-built DTB as base — no longer \
ships full prebuilt DTBs from Seeed's JP 5.1.4 driver pack."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "jetson-xavier-nx-a203"

SRC_URI = " \
    file://a203-overlay.dts \
    file://tegra19x-mb1-pinmux-p3668-a01.cfg \
"

DEPENDS = "dtc-native"

S = "${WORKDIR}"

do_compile() {
    dtc -I dts -O dtb -@ ${WORKDIR}/a203-overlay.dts -o ${WORKDIR}/a203-overlay.dtbo
}

inherit deploy

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${WORKDIR}/a203-overlay.dtbo ${DEPLOYDIR}/
    install -m 0644 ${WORKDIR}/tegra19x-mb1-pinmux-p3668-a01.cfg ${DEPLOYDIR}/a203-dtb-pinmux.cfg
}
addtask deploy before do_build after do_compile
