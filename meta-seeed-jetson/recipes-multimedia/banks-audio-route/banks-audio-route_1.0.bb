SUMMARY = "Banks AHUB audio route + DSP defaults"
DESCRIPTION = "systemd oneshot that wires the Tegra AHUB playback path \
through MVC1 (HW-ramped volume) and OPE1 (PEQ + MBDRC) before reaching I2S5. \
Establishes a single deterministic HW DSP path at boot. \
Both carriers wire a CS42448 TDM codec to I2S5/DAP5 on the 40-pin header, \
so the route is identical on devkit and A203."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "jetson-xavier-nx-(banks-devkit|a203)"

SRC_URI = " \
    file://banks-audio-route.sh \
    file://banks-audio-route.service \
    file://route.conf \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "banks-audio-route.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

RDEPENDS:${PN} = "alsa-utils"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/banks-audio-route.sh \
        ${D}${sbindir}/banks-audio-route

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/banks-audio-route.service \
        ${D}${systemd_system_unitdir}/banks-audio-route.service

    install -d ${D}${sysconfdir}/banks-audio
    install -m 0644 ${WORKDIR}/route.conf \
        ${D}${sysconfdir}/banks-audio/route.conf
}

FILES:${PN} = " \
    ${sbindir}/banks-audio-route \
    ${systemd_system_unitdir}/banks-audio-route.service \
    ${sysconfdir}/banks-audio/route.conf \
"
