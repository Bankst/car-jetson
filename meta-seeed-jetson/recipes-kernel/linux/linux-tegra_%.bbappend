FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append:jetson-xavier-nx-a203 = " \
    file://can.cfg \
    file://spi.cfg \
    file://usb-modem.cfg \
    file://usb-gadget.cfg \
"

