cmake_minimum_required(VERSION 3.20)
project(banks-viz VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- ImGui (no CMakeLists.txt in repo; compile sources manually) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# --- projectM (visualizer library + playlist) ---
set(ENABLE_GLES ON CACHE BOOL "" FORCE)
set(ENABLE_SYSTEM_GLM OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_THREADING ON CACHE BOOL "" FORCE)

FetchContent_Declare(libprojectM
    GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(libprojectM)
if(NOT libprojectm_POPULATED)
    FetchContent_Populate(libprojectM)
    add_subdirectory(${libprojectm_SOURCE_DIR} ${libprojectm_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# --- System dependencies ---
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(SDL2      REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(EGL       REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESV2    REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(PIPEWIRE  REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SYSTEMD   REQUIRED IMPORTED_TARGET libsystemd)

# --- ImGui static library from fetched sources ---
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC PkgConfig::SDL2 PkgConfig::GLESV2)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

# --- App ---
set(DEFAULT_PRESET_PATH "/usr/share/projectMSDL/presets" CACHE STRING
    "Default path to projectM preset directory")

add_executable(banks-viz
    src/main.cpp
    src/renderer.cpp
    src/audio_capture.cpp
    src/media_control.cpp
    src/volume_control.cpp
    src/gui.cpp
)

target_compile_definitions(banks-viz PRIVATE
    DEFAULT_PRESET_PATH="${DEFAULT_PRESET_PATH}"
)

# Playlist target from projectM (if built)
if(TARGET libprojectM::playlist)
    target_compile_definitions(banks-viz PRIVATE HAS_PROJECTM_PLAYLIST)
    target_link_libraries(banks-viz PRIVATE libprojectM::playlist)
endif()

target_link_libraries(banks-viz PRIVATE
    imgui
    PkgConfig::SDL2
    PkgConfig::EGL
    PkgConfig::GLESV2
    PkgConfig::PIPEWIRE
    PkgConfig::SYSTEMD
    libprojectM::projectM
    Threads::Threads
)

install(TARGETS banks-viz RUNTIME DESTINATION bin)
