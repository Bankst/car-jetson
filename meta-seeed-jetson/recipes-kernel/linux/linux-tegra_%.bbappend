FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:jetson-xavier-nx-a203 = " \
    file://can.cfg \
    file://spi.cfg \
    file://usb-modem.cfg \
    file://usb-gadget.cfg \
    file://dt-overlay.cfg \
    file://0002-nvgpu-drop-gcc13-implicit-fallthrough-override.patch \
"

# L4T 5.10 was authored for GCC 11; GCC 13 (scarthgap) promotes several new
# warnings to errors via -Werror.  Suppress the three that actually fire:
#   -Waddress        : trace/events/*.h macro NULL-address comparisons
#   -Wimplicit-fallthrough : acpica/dscontrol.c
#   -Wint-in-bool-context  : nvidia display driver ternary expressions
EXTRA_OEMAKE:append:jetson-xavier-nx-a203 = " KCFLAGS='-Wno-address -Wno-implicit-fallthrough -Wno-int-in-bool-context -Wno-tautological-compare'"

