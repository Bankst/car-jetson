# Shim: yocto-meta-kde master uses `inherit kf6-cmake` (hyphenated) but
# yocto-meta-kf6 master only ships kf6_cmake.bbclass (underscored). Forward
# the hyphenated name to the underscored implementation.
inherit kf6_cmake
