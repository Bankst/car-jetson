SUMMARY = "Banks Jetson Linux — KDE Plasma Wayland image"
DESCRIPTION = "Base image plus a minimal KDE Plasma 6 Wayland desktop."
LICENSE = "MIT"

require banks-jetson-image-base.bb

IMAGE_INSTALL:append = " packagegroup-de-plasma-minimal"

IMAGE_FEATURES:append = " splash"
