# Enable high-res Bluetooth audio codecs: AAC, aptX/aptX-HD, LDAC
PACKAGECONFIG:append = " bluez-aac bluez-aptx bluez-ldac"

# Pull in codec libs — fdk-aac is a DEPENDS of bluez-aac so it's automatic,
# but libopenaptx and libldac need explicit DEPENDS.
DEPENDS:append = " libfreeaptx libldac"
