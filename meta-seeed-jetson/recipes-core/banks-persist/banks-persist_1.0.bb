SUMMARY = "Persistent data partition (UDA) mount and bind mounts"
DESCRIPTION = "Mounts the NVMe UDA partition at /data via a systemd .mount \
unit, and bind-mounts persistent paths (bluetooth, ssh host keys) so they \
survive rootfs reflash. The UDA partition is pre-formatted at flash time \
by init-extra.d/50-format-uda.sh (installed by this recipe's bbappend on \
tegra-flash-init), so no on-boot format/setup service is needed."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://data.mount \
    file://banks-persist-bind \
    file://banks-persist-bind.service \
"

S = "${WORKDIR}"

inherit systemd

# data.mount unit name must match the mount path (data.mount → /data)
SYSTEMD_SERVICE:${PN} = "data.mount banks-persist-bind.service"
SYSTEMD_AUTO_ENABLE = "enable"

RDEPENDS:${PN} = "util-linux-mount"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/banks-persist-bind ${D}${sbindir}/banks-persist-bind

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/data.mount ${D}${systemd_system_unitdir}/data.mount
    install -m 0644 ${WORKDIR}/banks-persist-bind.service ${D}${systemd_system_unitdir}/
}

FILES:${PN} = " \
    ${sbindir}/banks-persist-bind \
    ${systemd_system_unitdir}/data.mount \
    ${systemd_system_unitdir}/banks-persist-bind.service \
"
