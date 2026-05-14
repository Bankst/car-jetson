SUMMARY = "BlueZ fixed-PIN A2DP sink agent"
DESCRIPTION = "Systemd daemon that registers a BlueZ pairing agent with a \
fixed PIN code and keeps the adapter permanently discoverable/pairable for \
use as a Bluetooth audio sink."
HOMEPAGE = "https://github.com/banks-troutman/car-jetson"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS = ""
RDEPENDS:${PN} = "python3-core python3-ctypes python3-dbus python3-logging bluez5"

SRC_URI = " \
    file://bt-audio-agent \
    file://bt-audio-agent.service \
    file://pin \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "bt-audio-agent.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

CONFFILES:${PN} = "${sysconfdir}/bluetooth/pin"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/bt-audio-agent ${D}${sbindir}/bt-audio-agent

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/bt-audio-agent.service \
        ${D}${systemd_system_unitdir}/bt-audio-agent.service

    install -d ${D}${sysconfdir}/bluetooth
    install -m 0600 ${WORKDIR}/pin ${D}${sysconfdir}/bluetooth/pin
}

FILES:${PN} = " \
    ${sbindir}/bt-audio-agent \
    ${systemd_system_unitdir}/bt-audio-agent.service \
    ${sysconfdir}/bluetooth/pin \
"
