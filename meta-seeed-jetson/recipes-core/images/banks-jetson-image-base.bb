SUMMARY = "Banks Jetson Linux — base image (no DE)"
DESCRIPTION = "Headless console image for Xavier NX on Seeed A203 V2. \
Includes NVIDIA Tegra userspace, NetworkManager, audio, base CLI tools, \
A203 interface bring-up. No display server / DE — see banks-jetson-image-plasma."
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += "ssh-server-openssh debug-tweaks"

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    packagegroup-seeed-base \
"

IMAGE_LINGUAS = "en-us"
