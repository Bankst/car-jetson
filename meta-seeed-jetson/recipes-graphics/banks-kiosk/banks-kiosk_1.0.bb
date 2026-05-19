SUMMARY = "Banks Jetson kiosk Wayland session launcher"
DESCRIPTION = "Starts weston (meta-tegra 10 / NVDC DRM backend) then executes \
/etc/kiosk/kiosk-app via kiosk-launcher wrapper. Replace kiosk-app for real binary."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://kiosk.service \
    file://kiosk-launcher \
    file://kiosk-app \
    file://kiosk-login-shell \
    file://toggle-desktop-mode \
    file://kiosk-keys.conf \
    file://weston-kiosk.ini \
    file://triggerhappy-override.conf \
"

S = "${WORKDIR}"

inherit systemd

RDEPENDS:${PN} = "bash"

SYSTEMD_SERVICE:${PN} = "kiosk.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${sysconfdir}/kiosk
    install -m 0755 ${WORKDIR}/kiosk-launcher ${D}${sysconfdir}/kiosk/kiosk-launcher
    install -m 0755 ${WORKDIR}/kiosk-app         ${D}${sysconfdir}/kiosk/kiosk-app
    install -m 0755 ${WORKDIR}/kiosk-login-shell ${D}${sysconfdir}/kiosk/kiosk-login-shell
    install -m 0644 ${WORKDIR}/weston-kiosk.ini     ${D}${sysconfdir}/kiosk/weston-kiosk.ini
    install -m 0755 ${WORKDIR}/toggle-desktop-mode ${D}${sysconfdir}/kiosk/toggle-desktop-mode

    install -d ${D}${sysconfdir}/triggerhappy/triggers.d
    install -m 0644 ${WORKDIR}/kiosk-keys.conf ${D}${sysconfdir}/triggerhappy/triggers.d/kiosk-keys.conf

    install -d ${D}${systemd_system_unitdir}/triggerhappy.service.d
    install -m 0644 ${WORKDIR}/triggerhappy-override.conf ${D}${systemd_system_unitdir}/triggerhappy.service.d/override.conf

    install -d ${D}/var/lib/kiosk

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/kiosk.service  ${D}${systemd_system_unitdir}/kiosk.service
}

FILES:${PN} = " \
    ${sysconfdir}/kiosk \
    ${sysconfdir}/triggerhappy/triggers.d/kiosk-keys.conf \
    ${systemd_system_unitdir}/kiosk.service \
    ${systemd_system_unitdir}/triggerhappy.service.d/override.conf \
    /var/lib/kiosk \
"
