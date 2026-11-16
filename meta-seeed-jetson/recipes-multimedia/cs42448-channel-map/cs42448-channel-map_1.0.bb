SUMMARY = "PipeWire drop-in: CS42448 8ch TDM channel-map / L-R swap"
DESCRIPTION = "Ships 20-cs42448-channel-map.conf to /etc/pipewire/pipewire.conf.d. \
HARDWARE-GATED / UNVERIFIED: the sink path predates the AHUB route and must be \
A/B-confirmed on board before adding to any image. Not pulled by any \
packagegroup on purpose."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://20-cs42448-channel-map.conf"

do_install() {
    install -d ${D}${sysconfdir}/pipewire/pipewire.conf.d
    install -m 0644 ${WORKDIR}/20-cs42448-channel-map.conf \
        ${D}${sysconfdir}/pipewire/pipewire.conf.d/20-cs42448-channel-map.conf
}

FILES:${PN} = "${sysconfdir}/pipewire/pipewire.conf.d/20-cs42448-channel-map.conf"
