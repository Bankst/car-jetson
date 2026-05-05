SUMMARY = "KDE Plasma Wayland minimal desktop"
DESCRIPTION = "Minimal Plasma 6 / KF6 / Qt6 desktop on Wayland. Ships the \
shell, panel, kicker, kwin compositor, system settings, file manager, \
terminal. Excludes kdepim, kdegames, kdeedu, calligra, krita, kdenlive, \
okular, etc. — pull those in separately via IMAGE_INSTALL if you need them."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit packagegroup

RDEPENDS:${PN} = " \
    \
    plasma-workspace \
    plasma-desktop \
    kwin \
    kscreen \
    systemsettings \
    breeze \
    breeze-icons \
    plasma-integration \
    \
    dolphin \
    konsole \
    \
    sddm \
    sddm-kcm \
    \
    xdg-desktop-portal-kde \
    xdg-desktop-portal-gtk \
    \
    qtwayland \
    \
    egl-gbm \
    egl-wayland \
    tegra-libraries-eglcore \
    tegra-libraries-glescore \
    tegra-libraries-gbm-backend \
    \
    noto-fonts \
"
