SUMMARY = "Seeed A203 V2 device tree binaries for Xavier NX"
DESCRIPTION = "Carrier-board-customized DTBs from Seeed's JP 5.1.4 driver \
pack for the A203 V2. Filenames match NVIDIA's stock so the meta-tegra \
linux-tegra bbappend can drop them in over the kernel-built variants."
LICENSE = "CLOSED"

COMPATIBLE_MACHINE = "jetson-xavier-nx-a203"

SRC_URI = " \
    file://tegra194-p3668-0000-p3509-0000.dtb \
    file://tegra194-p3668-0001-p3509-0000.dtb \
    file://tegra194-p3668-all-p3509-0000.dtb \
    file://tegra19x-mb1-pinmux-p3668-a01.cfg \
"

S = "${WORKDIR}"

inherit deploy

do_compile[noexec] = "1"

do_deploy() {
    install -d ${DEPLOYDIR}/a203-dtb
    install -m 0644 ${WORKDIR}/tegra194-p3668-*.dtb ${DEPLOYDIR}/a203-dtb/
    install -m 0644 ${WORKDIR}/tegra19x-mb1-pinmux-p3668-a01.cfg ${DEPLOYDIR}/a203-dtb/
}
addtask deploy before do_build after do_install
