FILESEXTRAPATHS:prepend := "${THISDIR}:"

SRC_URI:append:jetson-xavier-nx-a203 = " file://tegra19x-mb1-pinmux-p3668-a01.cfg"

# meta-tegra's tegra-bootfiles aggregates pinmux .cfg into the BCT. By placing
# the A203 pinmux earlier in FILESEXTRAPATHS we override the stock NVIDIA one.
