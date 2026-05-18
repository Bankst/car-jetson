SUMMARY = "Banks Jetson kiosk Wayland session launcher"
DESCRIPTION = "Starts weston (meta-tegra 10 / NVDC DRM backend) then executes \
/etc/kiosk/kiosk-app via kiosk-launcher wrapper. Replace kiosk-app for real binary."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://kiosk.service \
    file://kiosk-launcher \
    file://kiosk-app \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "kiosk.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${sysconfdir}/kiosk
    install -m 0755 ${WORKDIR}/kiosk-launcher ${D}${sysconfdir}/kiosk/kiosk-launcher
    install -m 0755 ${WORKDIR}/kiosk-app      ${D}${sysconfdir}/kiosk/kiosk-app

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/kiosk.service  ${D}${systemd_system_unitdir}/kiosk.service
}

FILES:${PN} = " \
    ${sysconfdir}/kiosk \
    ${systemd_system_unitdir}/kiosk.service \
"
