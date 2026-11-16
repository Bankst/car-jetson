SUMMARY = "Calm greeting chime at boot via ALSA APE card"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://banks-greeting-tone \
    file://banks-greeting-tone.service \
"

S = "${WORKDIR}"

inherit systemd

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/banks-greeting-tone ${D}${sbindir}/banks-greeting-tone

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/banks-greeting-tone.service ${D}${systemd_system_unitdir}/
}

SYSTEMD_SERVICE:${PN} = "banks-greeting-tone.service"
SYSTEMD_AUTO_ENABLE = "disable"

RDEPENDS:${PN} = "python3-core python3-math alsa-utils-aplay banks-audio-route"
