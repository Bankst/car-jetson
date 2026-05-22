SUMMARY = "Banks Jetson Linux — base image (no DE)"
DESCRIPTION = "Headless console image for Xavier NX on Seeed A203 V2. \
Includes NVIDIA Tegra userspace, NetworkManager, audio, base CLI tools, \
A203 interface bring-up. No display server / DE — see banks-jetson-image-plasma."
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-openssh debug-tweaks package-management"

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    packagegroup-base \
    packagegroup-seeed-base \
"

IMAGE_LINGUAS = ""

ROOTFS_POSTPROCESS_COMMAND:append = " banks_trim_rootfs; banks_bake_unit_fixes;"

banks_bake_unit_fixes() {
    # Mask systemd-networkd — NM owns all interfaces; networkd causes ~100s boot delay
    for unit in systemd-networkd.service systemd-networkd-wait-online.service \
                systemd-networkd.socket systemd-network-generator.service \
                NetworkManager-wait-online.service; do
        ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/${unit}
    done

    # Mask NetworkManager-wait-online — blocks network-online.target ~5.7s on
    # boots where NM hasn't fully settled (no Wi-Fi auth yet, link state in
    # flux). Nothing in our image legitimately needs network-online; userspace
    # services that talk to network re-resolve on first transient failure.
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/NetworkManager-wait-online.service

    # Boot-time noise reduction: mask services that nothing on this image
    # depends on but which still cost CPU + serialize on the multi-user.target
    # pull-up. Each saves 50-400ms of startup work.
    #   nvphs        : NVIDIA Performance HSD; 400ms cold start, only matters
    #                  if you use nvpmodel CLI to query throttling.
    #   sysstat      : sar log collection — not used on this image.
    #   rpcbind      : NFS portmapper — no NFS exports/mounts.
    #   busybox-syslog + busybox-klogd : journald already captures everything;
    #                  these dual-log to /var/log/messages for free disk wear.
    for unit in nvphs.service sysstat.service rpcbind.service rpcbind.socket \
                busybox-syslog.service busybox-klogd.service; do
        ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/${unit}
    done

    # /etc/resolv.conf -> systemd-resolved stub. Without this, dnsmasq spawned
    # by NM for l4tbr0 (method=shared) errors with "directory /etc/resolv.conf
    # for resolv-file is missing" each time the bridge enters forwarding state,
    # cascading into a usb0 slave thrash loop.
    ln -sf ../run/systemd/resolve/stub-resolv.conf \
        ${IMAGE_ROOTFS}${sysconfdir}/resolv.conf

    # Use pre-forked sshd (not socket-activated) — eliminates PAM thread IPC overhead.
    # UsePAM no in sshd_config means OpenSSH handles passwords natively; no logind
    # session needed (linger handles /run/user/UID for XDG).
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/sshd.socket
    install -d ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/multi-user.target.wants
    ln -sf /lib/systemd/system/sshd.service \
        ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/multi-user.target.wants/sshd.service

    # nvs-service: drop network-online.target dependency (sensor HAL idle on A203)
    install -d ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/nvs-service.service.d
    printf '[Unit]\nAfter=\nWants=\nAfter=nvstartup.service\n' \
        > ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/nvs-service.service.d/no-network-online.conf

    # Enable serial console login on ttyTHS0 (40-pin header pins 8/10, confirmed physical port)
    install -d ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/getty.target.wants
    ln -sf /lib/systemd/system/serial-getty@.service \
        ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/getty.target.wants/serial-getty@ttyTHS0.service

    # Allow root login on Tegra HSUARTs — not in upstream securetty list
    printf 'ttyTHS0\nttyTHS1\nttyTHS2\nttyTHS3\nttyTHS4\nttyTCU0\n' \
        >> ${IMAGE_ROOTFS}${sysconfdir}/securetty
}

banks_trim_rootfs() {
    for kdir in ${IMAGE_ROOTFS}${nonarch_base_libdir}/modules/*/kernel; do
        [ -d "$kdir" ] || continue
        rm -rf $kdir/drivers/infiniband
        rm -rf $kdir/drivers/media
        rm -rf $kdir/drivers/net/ethernet
        rm -rf $kdir/drivers/net/wireless/realtek
        rm -rf $kdir/drivers/net/wireless/intel
        rm -rf $kdir/fs/btrfs
        rm -rf $kdir/fs/cifs
        rm -rf $kdir/fs/nfsd
    done

    rm -rf ${IMAGE_ROOTFS}${nonarch_base_libdir}/udev/hwdb.bin
    rm -rf ${IMAGE_ROOTFS}${nonarch_base_libdir}/udev/hwdb.d

    rm -f ${IMAGE_ROOTFS}${bindir}/rsaperf
    rm -f ${IMAGE_ROOTFS}${bindir}/pk11ectest
    rm -f ${IMAGE_ROOTFS}${bindir}/fipstest
    rm -f ${IMAGE_ROOTFS}${bindir}/bltest
    rm -f ${IMAGE_ROOTFS}${bindir}/ecperf

    rm -rf ${IMAGE_ROOTFS}${datadir}/sounds
}
