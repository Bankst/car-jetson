FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://sshd_config \
    file://sshd.service \
"

do_install:append() {
    install -m 0600 ${WORKDIR}/sshd_config ${D}${sysconfdir}/ssh/sshd_config
    install -m 0644 ${WORKDIR}/sshd.service ${D}${systemd_system_unitdir}/sshd.service
}

FILES:${PN} += "${systemd_system_unitdir}/sshd.service"
