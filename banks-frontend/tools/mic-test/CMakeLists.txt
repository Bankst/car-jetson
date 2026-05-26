cmake_minimum_required(VERSION 3.23)
project(mic-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.7)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

qt_add_executable(mic-test main.cpp)

target_link_libraries(mic-test PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    PkgConfig::PIPEWIRE
)
