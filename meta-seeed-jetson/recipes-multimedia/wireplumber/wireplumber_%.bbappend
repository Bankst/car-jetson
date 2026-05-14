FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit systemd
SYSTEMD_SERVICE:${PN} = "wireplumber.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

SRC_URI:append = " \
    file://10-headless-bt.conf \
    file://20-bt-a2dp-only.conf \
    file://10-null-sink.conf \
    file://wireplumber-headless.service.conf \
    file://wireplumber-state.conf \
    file://pipewire-headless.service.conf \
"

do_install:append() {
    # WirePlumber config: disable seat-gated BT monitor for headless operation
    install -d ${D}${sysconfdir}/wireplumber/wireplumber.conf.d
    install -m 0644 ${WORKDIR}/10-headless-bt.conf \
        ${D}${sysconfdir}/wireplumber/wireplumber.conf.d/10-headless-bt.conf
    install -m 0644 ${WORKDIR}/20-bt-a2dp-only.conf \
        ${D}${sysconfdir}/wireplumber/wireplumber.conf.d/20-bt-a2dp-only.conf

    # PipeWire config: null sink fallback so BT streams always have a link target
    install -d ${D}${sysconfdir}/pipewire/pipewire.conf.d
    install -m 0644 ${WORKDIR}/10-null-sink.conf \
        ${D}${sysconfdir}/pipewire/pipewire.conf.d/10-null-sink.conf

    # systemd drop-in: point session bus to system bus (no user session on headless)
    install -d ${D}${systemd_system_unitdir}/wireplumber.service.d
    install -m 0644 ${WORKDIR}/wireplumber-headless.service.conf \
        ${D}${systemd_system_unitdir}/wireplumber.service.d/headless.conf

    install -d ${D}${systemd_system_unitdir}/pipewire.service.d
    install -m 0644 ${WORKDIR}/pipewire-headless.service.conf \
        ${D}${systemd_system_unitdir}/pipewire.service.d/headless.conf

    # tmpfiles.d: create wireplumber state dir owned by pipewire user
    install -d ${D}${libdir}/tmpfiles.d
    install -m 0644 ${WORKDIR}/wireplumber-state.conf \
        ${D}${libdir}/tmpfiles.d/wireplumber-state.conf
}

FILES:${PN}:append = " \
    ${sysconfdir}/wireplumber/wireplumber.conf.d/10-headless-bt.conf \
    ${sysconfdir}/wireplumber/wireplumber.conf.d/20-bt-a2dp-only.conf \
    ${sysconfdir}/pipewire/pipewire.conf.d/10-null-sink.conf \
    ${systemd_system_unitdir}/wireplumber.service.d/headless.conf \
    ${systemd_system_unitdir}/pipewire.service.d/headless.conf \
    ${libdir}/tmpfiles.d/wireplumber-state.conf \
"
