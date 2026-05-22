SUMMARY = "projectM - Milkdrop-compatible music visualizer library"
HOMEPAGE = "https://github.com/projectM-visualizer/projectm"
LICENSE = "LGPL-2.1-only"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=26f590fe167311fe2a5a7ce0b3e71900"

SRC_URI = "gitsm://github.com/projectM-visualizer/projectm.git;protocol=https;branch=master"
SRCREV = "4d2849333b63235a6af4d1f02508a97529d96dc7"
PV = "4.2.0+git"

S = "${WORKDIR}/git"

inherit cmake

DEPENDS = "glm virtual/egl virtual/libgles3"

EXTRA_OECMAKE = " \
    -DENABLE_GLES=ON \
    -DENABLE_THREADING=ON \
    -DENABLE_SYSTEM_GLM=ON \
    -DBUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=ON \
"

FILES:${PN} += "${datadir}/projectM"
