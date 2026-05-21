# Banks-customized UEFI for Jetson Xavier NX L4T R35.6.
#
# All source-mutating customizations are applied via do_unpack postfuncs in
# Python with binary-mode I/O — the edk2-nvidia tree is CRLF-encoded and
# LF-encoded quilt patches fail with "different line endings".
#
# Customizations:
#
#   1. banks_dsc_timeout_zero — sets PcdPlatformBootTimeOut compile-time
#      default to 0. Eliminates the L4TLauncher 5s countdown. Survives EFI
#      variable resets because the PCD is the *source* of the initial
#      Timeout var value.
#
#   2. banks_skip_display_gop — short-circuits IsPciDisplay() to return
#      FALSE and AddOutput() to no-op in PlatformBm.c, killing the HDMI/GOP
#      probe + the GOP-based ConOut registration. Serial console (ttyTCU0)
#      registers separately via PlatformRegisterConsoles. Linux drives the
#      display via DRM/KMS post-handoff.
#
#   3. banks_uefi_quiet — narrows the DEBUG output path:
#        - PcdDebugPropertyMask|0x41 (the RELEASE upstream default) leaves
#          DEBUG_PRINT_ENABLED bit OFF, so DEBUG() macros are LTO'd away.
#          This is the biggest UEFI-time saver — kills the per-driver-load
#          `add-symbol-file ...` UART blocking (~17ms × ~30 drivers ≈ 0.5s
#          direct + cascade effects).
#        - PcdDebugPrintErrorLevel narrowed to ERROR-only as belt-and-
#          suspenders for any library that bypasses the property-mask gate.
#
# Splash suppression: the upstream LF-encoded splash patch
# (`0001-banks-suppress-uefi-splash.patch`) skips BootLogoEnableLogo /
# DisplaySystemAndHotkeyInformation / MemoryTest in PlatformBootManager
# AfterConsole.
#
# Things tried in 2026-05-20 boot-opt session that DID NOT WORK on R35.6
# (documented in ~/.claude/projects/-home-bankst/memory/reference_jetson_flash_success_model.md):
#   - Wholesale FDF DXE driver trim broke EDK2 dispatch order (DTM ASSERT)
#   - OP-TEE "soft disable" via VariableSmmRuntimeDxe removal +
#     PcdEmuVariableNvModeEnable=TRUE caused unrecoverable boot loop

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://0001-banks-suppress-uefi-splash.patch;patchdir=../edk2-nvidia"

do_unpack[postfuncs] += "banks_dsc_timeout_zero banks_skip_display_gop banks_uefi_quiet"

banks_dsc_timeout_zero() {
    sed -i 's#PcdPlatformBootTimeOut|L"Timeout"|gEfiGlobalVariableGuid|0x0|5#PcdPlatformBootTimeOut|L"Timeout"|gEfiGlobalVariableGuid|0x0|0#' \
        ${S}/../edk2-nvidia/Platform/NVIDIA/NVIDIA.common.dsc.inc
}

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

banks_uefi_quiet() {
    # PcdDebugPropertyMask|0x41 is the RELEASE upstream default — leave it.
    # PcdDebugPrintErrorLevel default 0x8000000F → ERROR-only 0x80000000.
    sed -i 's#PcdDebugPrintErrorLevel|0x8000000F#PcdDebugPrintErrorLevel|0x80000000#' \
        ${S}/../edk2-nvidia/Platform/NVIDIA/NVIDIA.common.dsc.inc
}
