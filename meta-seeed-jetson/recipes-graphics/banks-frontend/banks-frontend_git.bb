SUMMARY = "Banks Infotainment Frontend (Qt6 Quick)"
DESCRIPTION = "Qt6/QML infotainment shell with projectM audio visualizer, \
Android Auto integration (aasdk/openauto), PipeWire audio, FFmpeg H264 \
decode, BlueZ BT management, ImGui debug overlay, and kissfft spectrum \
analyzer."
HOMEPAGE = "https://github.com/bankst/car-jetson"
LICENSE = "CLOSED"

inherit externalsrc qt6-cmake pkgconfig systemd

EXTERNALSRC = "${TOPDIR}/../banks-frontend"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

# Qt6 modules: qtbase (Core, Gui, Network, DBus), qtdeclarative (Qml, Quick,
# QuickControls2), qtconnectivity (Bluetooth).  qt6-cmake prepends
# qtbase-native automatically.
DEPENDS = " \
    qtbase \
    qtdeclarative \
    qtdeclarative-native \
    qtwayland \
    qtconnectivity \
    protobuf \
    protobuf-native \
    boost \
    openssl \
    libusb1 \
    pipewire \
    ffmpeg \
    gpsd \
    systemd \
    virtual/egl \
    virtual/libgles3 \
"

# GPS passthrough for Android Auto navigation (optional)
# NVDEC: hardware H264 decode via NVIDIA V4L2 API on Jetson
PACKAGECONFIG ??= "gps nvdec"
PACKAGECONFIG[gps] = "-DBANKS_AA_GPS=ON,-DBANKS_AA_GPS=OFF,gpsd"
PACKAGECONFIG[nvdec] = "-DBANKS_AA_NVDEC=ON,-DBANKS_AA_NVDEC=OFF,tegra-mmapi v4l-utils tegra-libraries-multimedia-utils tegra-libraries-multimedia,tegra-libraries-multimedia-v4l tegra-libraries-multimedia-utils tegra-libraries-multimedia libv4l"

# Point CMake FetchContent at the pre-fetched source directories so it never
# hits the network during do_configure / do_compile.
EXTRA_OECMAKE += " \
    -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
    -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
    -DENABLE_INSTALL=ON \
    -DBANKS_FRONTEND_FAVORITES_PATH=/data/banks-frontend/favorites.json \
    -DPROTOBUF_PROTOC_EXECUTABLE=${STAGING_BINDIR_NATIVE}/protoc \
    -DProtobuf_PROTOC_EXECUTABLE=${STAGING_BINDIR_NATIVE}/protoc \
"

# CMake installs the service to ${libdir}/systemd/system via
# CMAKE_INSTALL_LIBDIR, but the systemd bbclass expects it under
# ${systemd_system_unitdir} (/lib/systemd/system on non-usrmerge).
do_install:append() {
    if [ -f ${D}${libdir}/systemd/system/banks-frontend.service ] && \
       [ "${libdir}/systemd/system" != "${systemd_system_unitdir}" ]; then
        install -d ${D}${systemd_system_unitdir}
        mv ${D}${libdir}/systemd/system/banks-frontend.service \
           ${D}${systemd_system_unitdir}/
        rmdir --ignore-fail-on-non-empty ${D}${libdir}/systemd/system ${D}${libdir}/systemd 2>/dev/null || true
    fi
}

SYSTEMD_SERVICE:${PN} = "banks-frontend.service"
SYSTEMD_AUTO_ENABLE = "enable"

# NVIDIA package renames (libnvidia-egl-gbm etc.) require machine-arch
PACKAGE_ARCH = "${MACHINE_ARCH}"

RDEPENDS:${PN} = " \
    qtbase \
    qtdeclarative \
    qtdeclarative-qmlplugins \
    qtwayland \
    qtconnectivity \
    pipewire \
    bluez5 \
    openssl \
    libusb1 \
    ffmpeg \
    boost-system \
    boost-log \
"

# libprojectM + kissfft are built as shared libs via FetchContent; their .so
# files are installed into ${libdir} by CMake's install(TARGETS ... EXPORT).
FILES:${PN} += " \
    ${datadir}/banks-frontend \
    ${libdir}/libprojectM*.so* \
    ${libdir}/libkissfft*.so* \
"
# QML module plugin installed into Qt6 qml import path
FILES:${PN} += "${QT6_INSTALL_QMLDIR}/BanksFrontend"

# FetchContent builds libprojectM inline — conflicts with standalone recipe
RCONFLICTS:${PN} = "libprojectm"
RPROVIDES:${PN} = "libprojectm"

# Suppress dev-so QA for the FetchContent-built shared libs (no -dev split)
INSANE_SKIP:${PN} += "dev-so"
