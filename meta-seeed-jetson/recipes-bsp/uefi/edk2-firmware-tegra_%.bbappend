# Banks: suppress the UEFI splash phase emitted by L4T R35 BDS.
#
# Targets PlatformBootManagerAfterConsole in
#   edk2-nvidia/Silicon/NVIDIA/Library/PlatformBootManagerLib/PlatformBm.c
#
# Skips:
#   - BootLogoEnableLogo()
#   - DisplaySystemAndHotkeyInformation()
#   - MemoryTest()
#
# Risk: low. ConOut still produces serial text output from L4TLauncher and the
# kernel EFI stub. The Timeout EFI variable is already 0 so BdsWait doesn't
# spin. BootOrder enumeration and L4TLauncher boot path are untouched.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://0001-banks-suppress-uefi-splash.patch;patchdir=../edk2-nvidia"

# Force PcdPlatformBootTimeOut = 0 in NVIDIA's DSC. The file is CRLF-encoded
# upstream, which breaks LF-encoded quilt patches. Apply via sed after unpack
# instead. Eliminates L4TLauncher 5s countdown; survives EFI variable resets
# because the PCD is the *source* of the initial Timeout var value.
do_unpack[postfuncs] += "banks_dsc_timeout_zero banks_skip_display_gop banks_bds_trace"

# Temporary: bump DebugPrintErrorLevel + add timestamped milestone prints in
# PlatformBootManagerBeforeConsole to identify what consumes the BDS silent
# stretch between OP-TEE init and "Test Key" warning (~7s). Each milestone
# emits a DEBUG_ERROR-level line (always shown regardless of PCD) which the
# host UART logger timestamps with [+NN.NNN].
#
# Revert this postfunc once the bottleneck is identified.
banks_bds_trace() {
    # RELEASE build sets PcdDebugPropertyMask=0x41 (no DEBUG_PRINT_ENABLED bit 1)
    # which compiles out every DEBUG() macro. Flip bit 1 so our trace prints
    # actually emit. Keep RELEASE target; just enable PRINT.
    sed -i 's#PcdDebugPropertyMask|0x41#PcdDebugPropertyMask|0x43#' ${S}/../edk2-nvidia/Platform/NVIDIA/NVIDIA.common.dsc.inc
    # Keep PcdDebugPrintErrorLevel at default (ERROR + INIT + WARN + LOAD).
    # Our BANKS-TRACE prints use DEBUG_ERROR which is always emitted at this
    # level. Avoid 0xFFFFFFFF — verbose floods 115200 UART and causes
    # buffer overruns / interleaved text. Bump only if we need DXE dispatch
    # detail.

    PBM=${S}/../edk2-nvidia/Silicon/NVIDIA/Library/PlatformBootManagerLib/PlatformBm.c
    python3 -c '
import sys, re
p = sys.argv[1]
with open(p, "rb") as f: src = f.read()
nl = b"\r\n" if b"\r\n" in src else b"\n"

# Insert DEBUG print before/after each FilterAndProcess call in
# PlatformBootManagerBeforeConsole so we can see how long each costs.
markers = [
    (b"FilterAndProcess (&gEfiPciRootBridgeIoProtocolGuid, NULL, Connect);",
        b"BANKS-TRACE: before PciRootBridge Connect",
        b"BANKS-TRACE: after  PciRootBridge Connect"),
    (b"FilterAndProcess (&gEfiPciIoProtocolGuid, IsPciDisplay, Connect);",
        b"BANKS-TRACE: before PciDisplay Filter+Connect",
        b"BANKS-TRACE: after  PciDisplay Filter+Connect"),
    (b"FilterAndProcess (&gEfiGraphicsOutputProtocolGuid, NULL, AddOutput);",
        b"BANKS-TRACE: before GOP AddOutput",
        b"BANKS-TRACE: after  GOP AddOutput"),
    (b"EfiBootManagerConnectAll ();",
        b"BANKS-TRACE: before EfiBootManagerConnectAll",
        b"BANKS-TRACE: after  EfiBootManagerConnectAll"),
    (b"EfiBootManagerRefreshAllBootOption ();",
        b"BANKS-TRACE: before RefreshAllBootOption",
        b"BANKS-TRACE: after  RefreshAllBootOption"),
    (b"PlatformRegisterConsoles (PcdGetBool (PcdDoInitialConsoleRegistration));",
        b"BANKS-TRACE: before PlatformRegisterConsoles",
        b"BANKS-TRACE: after  PlatformRegisterConsoles"),
    (b"EfiBootManagerDispatchDeferredImages ();",
        b"BANKS-TRACE: before DispatchDeferredImages",
        b"BANKS-TRACE: after  DispatchDeferredImages"),
]

def wrap(text, target, before, after):
    line_pat = re.compile(rb"(^[ \t]*)" + re.escape(target) + rb"[ \t]*(\r?\n)", re.MULTILINE)
    def sub(m):
        indent = m.group(1)
        crlf = m.group(2)
        return (indent + b"DEBUG ((DEBUG_ERROR, \"%a\\r\\n\", \"" + before + b"\"));" + crlf
                + m.group(0)
                + indent + b"DEBUG ((DEBUG_ERROR, \"%a\\r\\n\", \"" + after + b"\"));" + crlf)
    new, n = line_pat.subn(sub, text, count=1)
    if n != 1:
        sys.stderr.write(f"banks_bds_trace: failed to wrap {target!r}\n")
    return new

for tgt, bef, aft in markers:
    src = wrap(src, tgt, bef, aft)

with open(p, "wb") as f: f.write(src)
' ${PBM}
}
banks_dsc_timeout_zero() {
    sed -i 's#PcdPlatformBootTimeOut|L"Timeout"|gEfiGlobalVariableGuid|0x0|5#PcdPlatformBootTimeOut|L"Timeout"|gEfiGlobalVariableGuid|0x0|0#' ${S}/../edk2-nvidia/Platform/NVIDIA/NVIDIA.common.dsc.inc
}

# Skip HDMI/GOP probe in PlatformBootManagerBeforeConsole. The dominant cost
# between OP-TEE init and L4TLauncher (~7s) is the PCI display device connect
# and the GOP-based ConOut registration. Force IsPciDisplay() to short-circuit
# false so no display device gets connected; force AddOutput() to no-op so no
# GOP handle gets added to ConOut. The FilterAndProcess callers stay in place
# so the functions remain used (avoid -Werror=unused-function), but their
# bodies become trivial. Serial console (ttyTCU0) is unaffected — it's
# registered separately by PlatformRegisterConsoles. Linux drives HDMI via
# DRM/KMS after kernel handoff regardless.
banks_skip_display_gop() {
    python3 -c '
import re, sys
p = sys.argv[1]
with open(p, "rb") as f: src = f.read()
nl = b"\r\n" if b"\r\n" in src else b"\n"

def inject(text, after_decl, payload):
    pat = (after_decl + r"[ \t]*\r?\n[ \t]*\{[ \t]*\r?\n").encode()
    def sub(m):
        return m.group(0) + (b"  " + payload + nl)
    new, n = re.subn(pat, sub, text, count=1)
    if n != 1:
        raise SystemExit(f"banks_skip_display_gop: failed to patch {after_decl}")
    return new

src = inject(src, r"IsPciDisplay \(\s*IN EFI_HANDLE\s+Handle,\s*IN CONST CHAR16\s+\*ReportText\s*\)", b"return FALSE;")
src = inject(src, r"AddOutput \(\s*IN EFI_HANDLE\s+Handle,\s*IN CONST CHAR16\s+\*ReportText\s*\)", b"return;")
with open(p, "wb") as f: f.write(src)
' ${S}/../edk2-nvidia/Silicon/NVIDIA/Library/PlatformBootManagerLib/PlatformBm.c
}
