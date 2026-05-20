SUMMARY = "Persistent data partition (UDA) setup and bind mounts"
DESCRIPTION = "Formats the NVMe UDA partition on first boot, adds it \
to fstab, mounts at /data, and bind-mounts persistent paths (bluetooth, \
ssh) so they survive rootfs reflash."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://banks-persist-setup \
    file://banks-persist-setup.service \
    file://banks-persist-bind \
    file://banks-persist-bind.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "banks-persist-setup.service banks-persist-bind.service"
SYSTEMD_AUTO_ENABLE = "enable"

RDEPENDS:${PN} = "e2fsprogs-mke2fs util-linux-blkid util-linux-mount"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/banks-persist-setup ${D}${sbindir}/banks-persist-setup
    install -m 0755 ${WORKDIR}/banks-persist-bind  ${D}${sbindir}/banks-persist-bind

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/banks-persist-setup.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/banks-persist-bind.service  ${D}${systemd_system_unitdir}/
}

FILES:${PN} = " \
    ${sbindir}/banks-persist-setup \
    ${sbindir}/banks-persist-bind \
    ${systemd_system_unitdir}/banks-persist-setup.service \
    ${systemd_system_unitdir}/banks-persist-bind.service \
"
