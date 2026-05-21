# Install the UDA-format script into the flashing initrd's /init-extra.d/.
# meta-tegra's init-extra.sh executes every script in that directory in
# lexical order after partitions are written and before reboot. Moving the
# format/seed step here eliminates the per-boot banks-persist-setup.service
# on the rootfs (saving ~1-3s on every boot).

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

PR = "r1"

SRC_URI:append = " file://05-fix-partition-table.sh file://50-format-uda.sh"

do_install:append() {
    install -m 0755 ${WORKDIR}/05-fix-partition-table.sh ${D}/init-extra.d/05-fix-partition-table.sh
    install -m 0755 ${WORKDIR}/50-format-uda.sh ${D}/init-extra.d/50-format-uda.sh
}
