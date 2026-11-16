# Banks-customized OP-TEE for Jetson Xavier NX L4T R35.6.
#
# The upstream Tegra OP-TEE conf.mk uses $(call force, KEY, VAL) which
# overrides anything passed via EXTRA_OEMAKE — so command-line make var
# overrides do not stick. Patch the source instead via do_unpack postfunc
# (matches the edk2-firmware-tegra bbappend pattern).
#
# Win target: kill OP-TEE TCU log spam. Pre-patch, every UEFI variable
# read/write fires "I/TC: WARNING: UEFI variable protection is not fully
# enabled !" over TCU; the per-boot log shows dozens of such lines. TCU
# throughput is the boot bottleneck. Drop CFG_TEE_CORE_LOG_LEVEL from 2
# (INFO) to 0 (LOG_LEVEL_NONE) — silences all DMSG/IMSG/EMSG paths.

do_unpack[postfuncs] += "banks_optee_quiet"

banks_optee_quiet() {
    sed -i \
        -e 's#force,CFG_TEE_CORE_LOG_LEVEL,2#force,CFG_TEE_CORE_LOG_LEVEL,0#' \
        ${S}/core/arch/arm/plat-tegra/conf.mk
}
