SUMMARY = "Minimal Wayland kiosk stack"
DESCRIPTION = "cage single-app Wayland compositor + kiosk launcher. L4T R35 \
dropped X11 DDX — Wayland via EGL/GBM is the only display path on JP5 Tegra. \
Replace kiosk-app script with real binary (Wayland-native or XWayland-wrapped)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

RDEPENDS:${PN} = " \
    weston \
    \
    egl-gbm \
    egl-wayland \
    tegra-libraries-eglcore \
    tegra-libraries-glescore \
    tegra-libraries-gbm-backend \
    \
    banks-kiosk \
    triggerhappy \
    matchbox-terminal \
    banks-frontend \
"
