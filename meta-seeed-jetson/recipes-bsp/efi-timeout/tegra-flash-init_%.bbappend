# Install the EFI Timeout=0 writer into the flashing initrd's /init-extra.d/.
# Runs once at flash time and writes to QSPI, where the value persists. The
# corresponding rootfs efi-timeout.service is disabled (still shipped for
# backstop, but SYSTEMD_AUTO_ENABLE = "disable").

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://30-efi-timeout.sh"

do_install:append() {
    install -m 0755 ${WORKDIR}/30-efi-timeout.sh ${D}/init-extra.d/30-efi-timeout.sh
}
