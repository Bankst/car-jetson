cmake_minimum_required(VERSION 3.23)
project(banks-frontend VERSION 0.1.0 LANGUAGES CXX)

# Default prefix /usr (matches Yocto staging). Must precede GNUInstallDirs.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "" FORCE)
endif()

include(GNUInstallDirs)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Qt6 ---
find_package(Qt6 6.7 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Network Bluetooth DBus
)
qt_standard_project_setup(REQUIRES 6.7)

# --- libprojectM ---
set(ENABLE_GLES         ON  CACHE BOOL "" FORCE)
set(ENABLE_SYSTEM_GLM   OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING       OFF CACHE BOOL "" FORCE)
set(ENABLE_THREADING    ON  CACHE BOOL "" FORCE)
FetchContent_Declare(libprojectM
    GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libprojectM)

# --- KissFFT (spectrum analyzer) ---
set(KISSFFT_TEST          OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS         OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG     OFF CACHE BOOL "" FORCE)
set(KISSFFT_DATATYPE      "float" CACHE STRING "" FORCE)
set(KISSFFT_OPENMP        OFF CACHE BOOL "" FORCE)
# libprojectM forces BUILD_SHARED_LIBS=ON; kissfft refuses static under
# that condition. Force kissfft shared too.
set(KISSFFT_STATIC        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(kissfft)

# --- Dear ImGui (debug overlay only) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

# --- Cream of the Crop presets ---
set(BANKS_FRONTEND_FETCH_PRESETS ON CACHE BOOL "Fetch curated preset pack")
set(BANKS_FRONTEND_PRESET_INSTALL_DIR
    "${CMAKE_INSTALL_DATADIR}/banks-frontend/presets" CACHE STRING "")

if(BANKS_FRONTEND_FETCH_PRESETS)
    FetchContent_Declare(presets_cream
        GIT_REPOSITORY https://github.com/projectM-visualizer/presets-cream-of-the-crop.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(presets_cream)
    install(DIRECTORY ${presets_cream_SOURCE_DIR}/
            DESTINATION ${BANKS_FRONTEND_PRESET_INSTALL_DIR}
            FILES_MATCHING
                PATTERN "*.milk"
                PATTERN "*.prjm"
                PATTERN ".git*" EXCLUDE
                PATTERN "*.md"  EXCLUDE
    )
endif()

# --- System deps (early — needed by aasdk) ---
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)

# --- aasdk + openauto (Android Auto protocol) ---
find_package(Protobuf REQUIRED)
find_package(Boost REQUIRED COMPONENTS system log_setup log)
find_package(OpenSSL REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)

# aap_protobuf: protocol buffer library from aasdk repo
FetchContent_Declare(aasdk
    GIT_REPOSITORY https://github.com/Bankst/aasdk.git
    GIT_TAG        bankst-dev
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(aasdk)
# GPS support (optional — enables SensorService for AA navigation)
find_library(GPS_LIBRARY gps)
find_path(GPS_INCLUDE_DIR gps.h)
option(BANKS_AA_GPS "Enable GPS passthrough for Android Auto" ${GPS_LIBRARY})
add_subdirectory(${aasdk_SOURCE_DIR}/protobuf ${aasdk_BINARY_DIR}/protobuf)

# aasdk-lib: core protocol library (bypasses upstream CMakeLists)
file(GLOB_RECURSE AASDK_SOURCES ${aasdk_SOURCE_DIR}/src/*.cpp)
list(FILTER AASDK_SOURCES EXCLUDE REGEX ".*\\.ut\\.cpp$")
add_library(aasdk-lib STATIC ${AASDK_SOURCES})
target_include_directories(aasdk-lib
    PUBLIC
        ${aasdk_SOURCE_DIR}/include
        ${aasdk_SOURCE_DIR}/include/aasdk
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/aa/compat
)
target_link_libraries(aasdk-lib PUBLIC
    aap_protobuf
    ${Protobuf_LIBRARIES}
    Boost::system Boost::log_setup Boost::log
    OpenSSL::SSL OpenSSL::Crypto
    PkgConfig::LIBUSB
)
target_compile_definitions(aasdk-lib PUBLIC BOOST_ALL_DYN_LINK)
set_target_properties(aasdk-lib PROPERTIES CXX_STANDARD 17)
target_compile_options(aasdk-lib PUBLIC -Wno-template-body)

# openauto-core: cherry-picked service/projection sources (no Qt5 UI)
FetchContent_Declare(openauto
    GIT_REPOSITORY https://github.com/Bankst/openauto.git
    GIT_TAG        qt6
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(openauto)

set(OA ${openauto_SOURCE_DIR}/src/autoapp)
set(OPENAUTO_CORE_SOURCES
    ${OA}/Service/AndroidAutoEntity.cpp
    ${OA}/Service/AndroidAutoEntityFactory.cpp
    ${OA}/Service/Pinger.cpp
    ${OA}/Service/MediaSink/AudioMediaSinkService.cpp
    ${OA}/Service/MediaSink/GuidanceAudioService.cpp
    ${OA}/Service/MediaSink/MediaAudioService.cpp
    ${OA}/Service/MediaSink/SystemAudioService.cpp
    ${OA}/Service/MediaSink/TelephonyAudioService.cpp
    ${OA}/Service/MediaSink/VideoMediaSinkService.cpp
    ${OA}/Service/MediaSink/VideoService.cpp
    ${OA}/Service/MediaSource/MediaSourceService.cpp
    ${OA}/Service/MediaSource/MicrophoneMediaSourceService.cpp
    ${OA}/Service/InputSource/InputSourceService.cpp
    ${OA}/Service/Bluetooth/BluetoothService.cpp
    ${OA}/Service/WifiProjection/WifiProjectionService.cpp
    ${OA}/Service/GenericNotification/GenericNotificationService.cpp
    ${OA}/Service/MediaBrowser/MediaBrowserService.cpp
    ${OA}/Service/MediaPlaybackStatus/MediaPlaybackStatusService.cpp
    ${OA}/Service/NavigationStatus/NavigationStatusService.cpp
    ${OA}/Service/PhoneStatus/PhoneStatusService.cpp
    ${OA}/Service/Radio/RadioService.cpp
    ${OA}/Service/VendorExtension/VendorExtensionService.cpp
    ${OA}/App.cpp
    ${OA}/Projection/VideoOutput.cpp
    ${OA}/Projection/DummyBluetoothDevice.cpp
    ${OA}/Projection/LocalBluetoothDevice.cpp
    ${OA}/../btservice/AndroidBluetoothServer.cpp
    ${OA}/../btservice/AndroidBluetoothService.cpp
    ${OA}/../btservice/BluetoothHandler.cpp
)
if(BANKS_AA_GPS)
    list(APPEND OPENAUTO_CORE_SOURCES ${OA}/Service/Sensor/SensorService.cpp)
endif()

qt_wrap_cpp(OPENAUTO_MOC_SOURCES
    ${openauto_SOURCE_DIR}/include/f1x/openauto/autoapp/Projection/LocalBluetoothDevice.hpp
    ${openauto_SOURCE_DIR}/include/f1x/openauto/btservice/AndroidBluetoothServer.hpp
    ${openauto_SOURCE_DIR}/include/f1x/openauto/btservice/BluetoothHandler.hpp
    TARGET openauto-core
)
add_library(openauto-core STATIC ${OPENAUTO_CORE_SOURCES} ${OPENAUTO_MOC_SOURCES})
target_include_directories(openauto-core
    PUBLIC ${openauto_SOURCE_DIR}/include
)
target_link_libraries(openauto-core PUBLIC aasdk-lib Qt6::Core Qt6::Gui Qt6::Network Qt6::Bluetooth)
if(BANKS_AA_GPS)
    target_include_directories(openauto-core PRIVATE ${GPS_INCLUDE_DIR})
    target_link_libraries(openauto-core PRIVATE ${GPS_LIBRARY})
    target_compile_definitions(openauto-core PUBLIC BANKS_AA_HAS_GPS)
    message(STATUS "Android Auto GPS: enabled")
else()
    message(STATUS "Android Auto GPS: disabled (install gpsd-devel to enable)")
endif()

# --- System deps (remaining) ---
pkg_check_modules(PIPEWIRE  REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SYSTEMD   REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(AVCODEC   REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL    REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE   REQUIRED IMPORTED_TARGET libswscale)

# --- App ---
qt_add_executable(banks-frontend
    src/main.cpp
    src/Log.h                src/Log.cpp
    src/Visualizer.h         src/Visualizer.cpp
    src/AudioCapture.h       src/AudioCapture.cpp
    src/AudioRingConsumer.h
    src/Fft.h                src/Fft.cpp
    src/SpectrumWidget.h     src/SpectrumWidget.cpp
    src/Favorites.h          src/Favorites.cpp
    src/ImGuiOverlay.h       src/ImGuiOverlay.cpp
    src/imgui_impl_qt.h      src/imgui_impl_qt.cpp
    src/aa/AASessionController.h  src/aa/AASessionController.cpp
    src/aa/AAVideoItem.h          src/aa/AAVideoItem.cpp
    src/aa/AAVideoDecoder.h       src/aa/AAVideoDecoder.cpp
    src/aa/NvdecVideoOutput.h     src/aa/NvdecVideoOutput.cpp
    src/aa/QmlInputDevice.h       src/aa/QmlInputDevice.cpp
    src/aa/AAServiceFactory.h     src/aa/AAServiceFactory.cpp
    src/aa/AAConfiguration.h
    src/aa/PipeWireAudioOutput.h  src/aa/PipeWireAudioOutput.cpp
    src/aa/PipeWireAudioInput.h   src/aa/PipeWireAudioInput.cpp
    src/aa/AudioTestController.h  src/aa/AudioTestController.cpp
    src/aa/SystemInfo.h           src/aa/SystemInfo.cpp
    src/aa/BluetoothPairingAgent.h src/aa/BluetoothPairingAgent.cpp
    src/aa/BluetoothManager.h      src/aa/BluetoothManager.cpp
    src/aa/LogCapture.h            src/aa/LogCapture.cpp
)

# Optional compile-time override for the favorites file location.
# Empty = use ${binaryDir}/favorites.json at runtime.
set(BANKS_FRONTEND_FAVORITES_PATH "" CACHE STRING
    "Absolute path to favorites JSON. Empty = beside binary at runtime.")
if(NOT BANKS_FRONTEND_FAVORITES_PATH STREQUAL "")
    target_compile_definitions(banks-frontend PRIVATE
        BANKS_FRONTEND_FAVORITES_PATH="${BANKS_FRONTEND_FAVORITES_PATH}")
endif()

qt_add_qml_module(banks-frontend
    URI BanksFrontend
    VERSION 1.0
    IMPORTS QtQuick
    QML_FILES
        qml/Main.qml
        qml/HomePage.qml
        qml/VisualizerPage.qml
        qml/AndroidAutoPage.qml
        qml/SettingsPage.qml
        qml/SettingsStubPage.qml
        qml/SettingsAudioPage.qml
        qml/SettingsDisplayPage.qml
        qml/SettingsVizPage.qml
        qml/SettingsCanPage.qml
        qml/SettingsMediaPage.qml
        qml/SettingsSystemPage.qml
        qml/SettingsLogPage.qml
    RESOURCES
        qml/qtquickcontrols2.conf
)

target_compile_definitions(banks-frontend PRIVATE
    BANKS_FRONTEND_PRESET_PATH="${CMAKE_INSTALL_FULL_DATADIR}/banks-frontend/presets"
)

target_link_libraries(banks-frontend PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus
    imgui
    libprojectM::projectM
    libprojectM::playlist
    kissfft
    openauto-core
    PkgConfig::PIPEWIRE
    PkgConfig::SYSTEMD
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    Threads::Threads
)

install(TARGETS banks-frontend
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Service file (optional)
install(FILES systemd/banks-frontend.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    OPTIONAL
)
