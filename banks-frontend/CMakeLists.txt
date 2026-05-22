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
    Core Gui Qml Quick QuickControls2
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

# --- System deps ---
find_package(PkgConfig REQUIRED)
find_package(Threads   REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SYSTEMD  REQUIRED IMPORTED_TARGET libsystemd)

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
    RESOURCES
        qml/qtquickcontrols2.conf
)

target_compile_definitions(banks-frontend PRIVATE
    BANKS_FRONTEND_PRESET_PATH="${CMAKE_INSTALL_FULL_DATADIR}/banks-frontend/presets"
)

target_link_libraries(banks-frontend PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    imgui
    libprojectM::projectM
    libprojectM::playlist
    kissfft
    PkgConfig::PIPEWIRE
    PkgConfig::SYSTEMD
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
