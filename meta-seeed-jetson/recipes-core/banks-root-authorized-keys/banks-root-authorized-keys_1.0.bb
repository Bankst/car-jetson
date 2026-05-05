SUMMARY = "Bake Banks's pubkey into /root/.ssh/authorized_keys"
DESCRIPTION = "Pre-installs the operator's SSH public key for root so SSH \
works on first boot without per-flash ssh-copy-id. Pubkey auth bypasses \
sshd's default PermitEmptyPasswords=no, so this is what actually lets us \
SSH in even with debug-tweaks setting an empty root password."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://authorized_keys"

S = "${WORKDIR}"

do_install() {
    install -d -m 0700 -o root -g root ${D}/root/.ssh
    install -m 0600 -o root -g root ${WORKDIR}/authorized_keys ${D}/root/.ssh/authorized_keys
}

FILES:${PN} = " \
    /root/.ssh \
    /root/.ssh/authorized_keys \
"
