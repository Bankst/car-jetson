DESCRIPTION = "Seeed A203 V2 carrier board device tree for Xavier NX"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit devicetree

COMPATIBLE_MACHINE = "jetson-xavier-nx-a203"

FILESEXTRAPATHS:prepend := "${THISDIR}/custom:"

SRC_URI = " \
    file://tegra194-p3668-a203.dts \
    file://a203-sd.dtsi \
"

KERNEL_INCLUDE = " \
    ${STAGING_KERNEL_DIR}/nvidia/soc/tegra/kernel-include \
    ${STAGING_KERNEL_DIR}/nvidia/platform/tegra/common/kernel-dts \
    ${STAGING_KERNEL_DIR}/nvidia/soc/t19x/kernel-include \
    ${STAGING_KERNEL_DIR}/nvidia/soc/t19x/kernel-dts \
    ${STAGING_KERNEL_DIR}/nvidia/platform/t19x/common/kernel-dts \
    ${STAGING_KERNEL_DIR}/nvidia/platform/t19x/galen/kernel-dts \
    ${STAGING_KERNEL_DIR}/nvidia/platform/t19x/jakku/kernel-dts \
    ${STAGING_KERNEL_DIR}/nvidia/platform/t19x/mccoy/kernel-dts \
    ${STAGING_KERNEL_DIR}/scripts/dtc/include-prefixes \
"

DTC_PPFLAGS:append = " -DLINUX_VERSION=510 -DTEGRA_HOST1X_DT_VERSION=1"

# Override devicetree.bbclass expand_includes to use a list (preserves include order)
def expand_includes(varname, d):
    import glob
    includes = list()
    for i in (d.getVar(varname) or "").split():
        for g in glob.glob(i):
            if os.path.isdir(g):
                includes.append(g)
    return includes
