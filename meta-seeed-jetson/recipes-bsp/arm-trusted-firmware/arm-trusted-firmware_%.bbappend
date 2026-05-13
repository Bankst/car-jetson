# ATF 2.6 + GCC 13: bl31/ehf.c triggers -Werror=logical-op.
# ATF Makefile collects -Werror in the ERRORS variable (line 409); override
# it to add the specific suppression while keeping all other -Werror checks.
EXTRA_OEMAKE:append = " ERRORS='-Werror -Wno-error=logical-op'"
