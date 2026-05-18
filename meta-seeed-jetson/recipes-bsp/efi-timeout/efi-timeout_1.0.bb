SUMMARY = "First-boot service to set UEFI boot timeout to 0"
DESCRIPTION = "Writes Timeout EFI variable (UINT16=0) via efivarfs on first boot. \
Eliminates L4TLauncher countdown. Stamp at /var/lib/efi-timeout-configured prevents \
re-runs. L4T UEFI stores EFI vars in QSPI — persists across power cycles."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://efi-timeout.sh \
    file://efi-timeout.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "efi-timeout.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/efi-timeout.sh ${D}${sbindir}/efi-timeout.sh

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/efi-timeout.service ${D}${systemd_system_unitdir}/efi-timeout.service
}

FILES:${PN} = " \
    ${sbindir}/efi-timeout.sh \
    ${systemd_system_unitdir}/efi-timeout.service \
"
