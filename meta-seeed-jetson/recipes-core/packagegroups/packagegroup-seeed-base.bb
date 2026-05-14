SUMMARY = "Banks Jetson base package set (DE-agnostic)"
DESCRIPTION = "Everything needed for a usable headless Xavier NX rootfs on \
Banks Jetson Linux. No display server, no DE, no compositor. KDE Plasma, \
LXQt, Wayfire, etc. layer on top via their own packagegroups."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

RDEPENDS:${PN} = " \
    \
    kernel-modules \
    \
    networkmanager \
    networkmanager-nmcli \
    networkmanager-nmtui \
    iproute2 \
    iputils \
    \
    bluez5 \
    wireless-regdb-static \
    linux-firmware-qca \
    bt-audio-agent \
    \
    pipewire \
    pipewire-alsa \
    pipewire-pulse \
    wireplumber \
    alsa-utils \
    \
    util-linux \
    coreutils \
    findutils \
    grep sed gawk \
    bash \
    less \
    sudo \
    nano \
    htop \
    ncdu \
    rsync \
    openssh \
    ca-certificates \
    \
    e2fsprogs \
    e2fsprogs-resize2fs \
    parted \
    nvme-cli \
    \
    strace \
    ltrace \
    perf \
    lsof \
    iotop \
    pciutils \
    usbutils \
    tcpdump \
    gdb \
    gdbserver \
    iftop \
    bmon \
    nmap \
    powertop \
    stress-ng \
    sysstat \
    socat \
    netcat-openbsd \
    i2c-tools \
    can-utils \
    picocom \
    minicom \
    screen \
    tio \
    evtest \
    \
    banks-root-authorized-keys \
"

# Mask conflicting network managers in the rootfs.
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
