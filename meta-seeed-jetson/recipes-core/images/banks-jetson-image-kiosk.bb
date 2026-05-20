SUMMARY = "Banks Jetson Linux — minimal Wayland kiosk image"
DESCRIPTION = "Base image plus cage Wayland compositor kiosk session. \
Boots to graphical.target, auto-starts /etc/kiosk/kiosk-app fullscreen via cage. \
L4T R35 (JP5) Wayland-only — no X11 DDX. Replace /etc/kiosk/kiosk-app for real app."
LICENSE = "MIT"

require banks-jetson-image-base.bb

IMAGE_INSTALL:append = " packagegroup-kiosk"

IMAGE_FEATURES:append = " splash"

ROOTFS_POSTPROCESS_COMMAND:append = " banks_kiosk_target;"

banks_kiosk_target() {
    # Boot to graphical.target instead of multi-user.target
    ln -sf /lib/systemd/system/graphical.target \
        ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/default.target

    # Mask weston-init's service — kiosk.service owns the compositor
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/weston.service
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/weston.socket

    # Mask triggerhappy socket activation — thd must open devices directly
    # (socket mode ignores --deviceglob and waits for udev th-cmd passfd)
    ln -sf /dev/null ${IMAGE_ROOTFS}${sysconfdir}/systemd/system/triggerhappy.socket
}
