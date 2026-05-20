SUMMARY = "projectM - Milkdrop-compatible music visualizer library"
HOMEPAGE = "https://github.com/projectM-visualizer/projectm"
LICENSE = "LGPL-2.1-only"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=26f590fe167311fe2a5a7ce0b3e71900"

SRC_URI = "gitsm://github.com/projectM-visualizer/projectm.git;protocol=https;nobranch=1"
SRCREV = "3158ee615eaafd93a8912b5f6dd84a9c47b2e00a"

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

do_configure:prepend() {
    # SOIL2: X11 elif branch includes GL/glx.h on Linux even with GLES.
    # Add GLES guard to skip it (same pattern as the Win32 branch).
    sed -i 's/#elif defined( SOIL_X11_PLATFORM )/#elif !defined(SOIL_GLES1) \&\& !defined(SOIL_GLES2) \&\& !defined(SOIL_GLES3) \&\& defined( SOIL_X11_PLATFORM )/' \
        ${S}/vendor/SOIL2/SOIL2.c
}

FILES:${PN} += "${datadir}/projectM"
