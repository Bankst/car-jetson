SUMMARY = "LDAC Bluetooth audio codec library (encoder/decoder)"
DESCRIPTION = "ldacBT provides the LDAC Bluetooth audio codec at up to 990 kbps \
(96kHz/24-bit). Used by pipewire bluez5-codec-ldac."
HOMEPAGE = "https://github.com/EHfive/ldacBT"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

# gitsm:// fetches the libldac/ git submodule (Sony/AOSP source)
SRC_URI = "gitsm://github.com/EHfive/ldacBT.git;protocol=https;branch=master"
SRCREV = "af2dd23979453bcd1cad7c4086af5fb421a955c5"
PV = "2.0.2.3+git"

S = "${WORKDIR}/git"

inherit cmake pkgconfig

EXTRA_OECMAKE = " \
    -DLDAC_SOFT_FLOAT=OFF \
    -DINSTALL_LIBDIR=${libdir} \
    -DINSTALL_INCLUDEDIR=${includedir} \
    -DINSTALL_PKGCONFIGDIR=${libdir}/pkgconfig \
    -DINSTALL_LDAC_INCLUDEDIR=${includedir}/ldac \
"
