FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# Kernel feature config fragments — apply to ALL Xavier NX builds. Both A203
# and devkit expose CAN/SPI on the 40-pin header and support USB gadget mode.
# Userspace bring-up (interface configs, gadget script, NM keyfiles) lives
# in banks-jetson-iface and is machine-gated via MACHINE_EXTRA_RDEPENDS in
# each machine conf.
SRC_URI:append = " \
    file://can.cfg \
    file://spi.cfg \
    file://usb-modem.cfg \
    file://usb-gadget.cfg \
    file://dt-overlay.cfg \
    file://overlayfs.cfg \
    file://no-camera.cfg \
    file://no-bloat.cfg \
    file://no-debug.cfg \
    file://feature-adds.cfg \
"

# Audio kernel config: BOTH carriers run the CS42448 TDM codec on I2S5/DAP5
# via the 40-pin header (standard NVIDIA pinout), so both get the full SoC
# audio stack (AHUB/ADMAIF/I2S5/...) + the cs42xx8 codec driver.
# no-audio-soc.cfg is retained in-tree but unreferenced; re-gate a carrier to
# it to drop SoC audio for boot speed / ADSP log noise if the codec is removed.
SRC_URI:append:jetson-xavier-nx-a203          = " file://audio-soc.cfg file://cs42448.cfg"
SRC_URI:append:jetson-xavier-nx-banks-devkit  = " file://audio-soc.cfg file://cs42448.cfg"

# GCC 13 (scarthgap) compatibility — applies to ALL Xavier NX builds. L4T
# 5.10 was authored for GCC 11; GCC 13 promotes several new warnings to
# errors via -Werror. Suppressions that actually fire across machines:
#   -Waddress              : trace/events/*.h macro NULL-address comparisons
#   -Wimplicit-fallthrough : acpica/dscontrol.c, tegra dc/dsi.c (devkit)
#   -Wint-in-bool-context  : nvidia display driver ternary expressions
#   -Wtautological-compare : misc r8168 OOT module
SRC_URI:append = " file://0002-nvgpu-drop-gcc13-implicit-fallthrough-override.patch"
SRC_URI:append = " file://0003-hdmi-allow-portrait-resolutions-above-2160p.patch"
SRC_URI:append = " file://0004-cs42xx8-add-reset-pulse-width-delay.patch"
EXTRA_OEMAKE:append = " KCFLAGS='-Wno-address -Wno-implicit-fallthrough -Wno-int-in-bool-context -Wno-tautological-compare'"

