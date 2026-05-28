# ATF 2.6 + GCC 13: bl31/ehf.c triggers -Werror=logical-op.
# ATF Makefile collects -Werror in the ERRORS variable (line 409); override
# it to add the specific suppression while keeping all other -Werror checks.
EXTRA_OEMAKE:append = " ERRORS='-Werror -Wno-error=logical-op'"

# Silent BL31. Default in upstream recipe is 20 (VERBOSE) which streams every
# init step over the Tegra Combined UART. TCU throughput is the bottleneck;
# each NOTICE/INFO line is ~10ms wall time. Drop to 0 (LOG_LEVEL_NONE) =
# no prints at all. Errors that would still go via panic path still abort.
ATF_LOG_LEVEL = "0"
