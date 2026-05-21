SUMMARY = "A203 V2 interface bring-up: CAN0 @ 500kbps, SPIDEV, USB-modem modules"
DESCRIPTION = "Configures NetworkManager to bring up can0 at 500 kbps and \
loads spidev / can / usb-modem kernel modules at boot. Targets the A203 V2 \
carrier with Xavier NX."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "jetson-xavier-nx-a203"

SRC_URI = " \
    file://seeed-a203-modules.conf \
    file://can0-up.sh \
    file://can0-up.service \
    file://banks-usb-gadget.sh \
    file://banks-usb-gadget.service \
    file://90-banks-usb-gadget-managed.rules \
    file://l4tbr0.nmconnection \
    file://l4t-gadget-usb0.nmconnection \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "banks-usb-gadget.service can0-up.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

RDEPENDS:${PN} = "networkmanager iproute2 kmod"

do_install() {
    install -d ${D}${sysconfdir}/modules-load.d
    install -m 0644 ${WORKDIR}/seeed-a203-modules.conf \
        ${D}${sysconfdir}/modules-load.d/seeed-a203.conf

    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/can0-up.sh \
        ${D}${sbindir}/can0-up.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/can0-up.service \
        ${D}${systemd_system_unitdir}/can0-up.service

    install -d ${D}${nonarch_base_libdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/90-banks-usb-gadget-managed.rules \
        ${D}${nonarch_base_libdir}/udev/rules.d/90-banks-usb-gadget-managed.rules

    install -d ${D}${sysconfdir}/NetworkManager/system-connections
    install -m 0600 ${WORKDIR}/l4tbr0.nmconnection \
        ${D}${sysconfdir}/NetworkManager/system-connections/l4tbr0.nmconnection
    install -m 0600 ${WORKDIR}/l4t-gadget-usb0.nmconnection \
        ${D}${sysconfdir}/NetworkManager/system-connections/l4t-gadget-usb0.nmconnection

    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/banks-usb-gadget.sh \
        ${D}${sbindir}/banks-usb-gadget.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/banks-usb-gadget.service \
        ${D}${systemd_system_unitdir}/banks-usb-gadget.service
}

FILES:${PN} = " \
    ${sysconfdir}/modules-load.d/seeed-a203.conf \
    ${sbindir}/can0-up.sh \
    ${systemd_system_unitdir}/can0-up.service \
    ${nonarch_base_libdir}/udev/rules.d/90-banks-usb-gadget-managed.rules \
    ${sysconfdir}/NetworkManager/system-connections/l4tbr0.nmconnection \
    ${sysconfdir}/NetworkManager/system-connections/l4t-gadget-usb0.nmconnection \
    ${sbindir}/banks-usb-gadget.sh \
    ${systemd_system_unitdir}/banks-usb-gadget.service \
"
