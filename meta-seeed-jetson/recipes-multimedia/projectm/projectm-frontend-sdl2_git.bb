SUMMARY = "projectM SDL2 standalone visualizer frontend"
HOMEPAGE = "https://github.com/projectM-visualizer/frontend-sdl2"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://LICENSE.md;md5=9cb07c5bfd74f03f1500637760a49640"

SRC_URI = "gitsm://github.com/projectM-visualizer/frontend-sdl2.git;protocol=https;branch=master"
SRCREV = "${AUTOREV}"
PV = "0.0+git"

S = "${WORKDIR}/git"

inherit cmake pkgconfig

DEPENDS = "libprojectm libsdl2 poco freetype virtual/egl virtual/libgles3"

EXTRA_OECMAKE = " \
    -DENABLE_GLES=ON \
    -DENABLE_DESKTOP_ICON=OFF \
    -DBINARY_TO_COMPRESSED_EXECUTABLE=${WORKDIR}/binary_to_compressed_c \
"

RDEPENDS:${PN} = "libprojectm"

FILES:${PN} += "${datadir}/projectMSDL"

do_configure:prepend() {
    # Build imgui's binary_to_compressed_c as a native host tool
    ${BUILD_CXX} -o ${WORKDIR}/binary_to_compressed_c \
        ${S}/vendor/imgui/misc/fonts/binary_to_compressed_c.cpp

    # ImGui.cmake's ImGuiDemo target links OpenGL::GL which doesn't exist
    # without GLX. It's EXCLUDE_FROM_ALL so just neuter the reference.
    sed -i 's/OpenGL::GL/OpenGL::OpenGL/' ${S}/ImGui.cmake
}
