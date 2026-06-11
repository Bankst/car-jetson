SUMMARY = "Banks Infotainment Frontend (Qt6 Quick)"
DESCRIPTION = "Qt6/QML infotainment shell with projectM audio visualizer, \
Android Auto integration (aasdk/openauto), PipeWire audio, FFmpeg H264 \
decode, BlueZ BT management, ImGui debug overlay, and kissfft spectrum \
analyzer."
HOMEPAGE = "https://github.com/bankst/car-jetson"
LICENSE = "CLOSED"

# Main source: banks-frontend subdirectory of car-jetson repo
SRC_URI = "git://github.com/bankst/car-jetson.git;protocol=ssh;branch=worktree-aa-integration;name=main \
           gitsm://github.com/projectM-visualizer/projectm.git;protocol=https;branch=master;destsuffix=libprojectm;name=libprojectm \
           git://github.com/mborgerding/kissfft.git;protocol=https;branch=master;destsuffix=kissfft;name=kissfft \
           git://github.com/ocornut/imgui.git;protocol=https;nobranch=1;destsuffix=imgui;name=imgui \
           git://github.com/bankst/aasdk.git;protocol=https;branch=bankst-dev;destsuffix=aasdk;name=aasdk \
           git://github.com/bankst/openauto.git;protocol=https;branch=qt6;destsuffix=openauto;name=openauto \
           git://github.com/projectM-visualizer/presets-cream-of-the-crop.git;protocol=https;branch=master;destsuffix=presets-cream;name=presets \
           file://eq-profile.json \
"
SRCREV_main      = "3ef8f4fcd6c73f8e2ed3131d68315041fdaceedb"
SRCREV_libprojectm = "4d2849333b63235a6af4d1f02508a97529d96dc7"
SRCREV_kissfft   = "6398d8a1d0c92486b5ece8a456fd5e6a97ad1f08"
SRCREV_imgui     = "dbb5eeaadffb6a3ba6a60de1290312e5802dba5a"
SRCREV_aasdk     = "d338c403631e887ea9ad364fff8aa73f6e4aed31"
SRCREV_openauto  = "8df462fa312a4ea9a3c1f8e1eb111a2c8683a311"
SRCREV_presets   = "0180df21f5e0bd39b9060cc5de420ed2f1f9e509"
SRCREV_FORMAT = "main"

PV = "0.1.0+git"
S = "${WORKDIR}/git/banks-frontend"

inherit qt6-cmake pkgconfig systemd

# Qt6 modules: qtbase (Core, Gui, Network, DBus), qtdeclarative (Qml, Quick,
# QuickControls2), qtconnectivity (Bluetooth).  qt6-cmake prepends
# qtbase-native automatically.
DEPENDS = " \
    qtbase \
    qtdeclarative \
    qtdeclarative-native \
    qtconnectivity \
    protobuf \
    protobuf-native \
    boost \
    openssl \
    libusb1 \
    pipewire \
    alsa-lib \
    ffmpeg \
    systemd \
    virtual/egl \
    virtual/libgles3 \
"

# GPS passthrough for Android Auto navigation (optional)
PACKAGECONFIG ??= ""
PACKAGECONFIG[gps] = "-DBANKS_AA_GPS=ON,-DBANKS_AA_GPS=OFF,gpsd"

# Point CMake FetchContent at the pre-fetched source directories so it never
# hits the network during do_configure / do_compile.
EXTRA_OECMAKE += " \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_LIBPROJECTM=${WORKDIR}/libprojectm \
    -DFETCHCONTENT_SOURCE_DIR_KISSFFT=${WORKDIR}/kissfft \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI=${WORKDIR}/imgui \
    -DFETCHCONTENT_SOURCE_DIR_AASDK=${WORKDIR}/aasdk \
    -DFETCHCONTENT_SOURCE_DIR_OPENAUTO=${WORKDIR}/openauto \
    -DFETCHCONTENT_SOURCE_DIR_PRESETS_CREAM=${WORKDIR}/presets-cream \
    -DBANKS_FRONTEND_FETCH_PRESETS=ON \
    -DBANKS_FRONTEND_FAVORITES_PATH=/data/banks-frontend/favorites.json \
    -DPROTOBUF_PROTOC_EXECUTABLE=${STAGING_BINDIR_NATIVE}/protoc \
    -DProtobuf_PROTOC_EXECUTABLE=${STAGING_BINDIR_NATIVE}/protoc \
"

# CMake installs the service to ${libdir}/systemd/system via
# CMAKE_INSTALL_LIBDIR, but the systemd bbclass expects it under
# ${systemd_system_unitdir} (/lib/systemd/system on non-usrmerge).
do_install:append() {
    if [ -f ${D}${libdir}/systemd/system/banks-frontend.service ]; then
        install -d ${D}${systemd_system_unitdir}
        mv ${D}${libdir}/systemd/system/banks-frontend.service \
           ${D}${systemd_system_unitdir}/
        rmdir --ignore-fail-on-non-empty ${D}${libdir}/systemd/system ${D}${libdir}/systemd 2>/dev/null || true
    fi

    # Config-time EQ band map (loader falls back to cabin10 if absent; a
    # /data/banks-frontend/eq-profile.json overrides this at runtime).
    install -d ${D}${sysconfdir}/banks-audio
    install -m 0644 ${WORKDIR}/eq-profile.json ${D}${sysconfdir}/banks-audio/eq-profile.json
}

CONFFILES:${PN} = "${sysconfdir}/banks-audio/eq-profile.json"
FILES:${PN} += "${sysconfdir}/banks-audio/eq-profile.json"

SYSTEMD_SERVICE:${PN} = "banks-frontend.service"
SYSTEMD_AUTO_ENABLE = "enable"

# NVIDIA package renames (libnvidia-egl-gbm etc.) require machine-arch
PACKAGE_ARCH = "${MACHINE_ARCH}"

RDEPENDS:${PN} = " \
    qtbase \
    qtdeclarative \
    qtdeclarative-qmlplugins \
    qtconnectivity \
    pipewire \
    pipewire-tools \
    alsa-utils \
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
