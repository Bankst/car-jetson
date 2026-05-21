#!/bin/sh
# Set UEFI boot countdown to 0 on first boot.
# Writes Timeout EFI variable (UINT16=0) via efivarfs.
# L4T stores EFI vars in QSPI — this persists across power cycles.
EFIVAR=/sys/firmware/efi/efivars/Timeout-8be4df61-93ca-11d2-aa0d-00e098032b8c
STAMP=/var/lib/efi-timeout-configured

if ! [ -f "$EFIVAR" ]; then
    echo "efi-timeout: $EFIVAR not found, skipping" >&2
    exit 0
fi

# Re-check on every boot: bytes 4-5 are UINT16 timeout value (LE).
# If already zero AND stamp present, exit fast.
CURRENT=$(od -An -tx1 -N2 -j4 "$EFIVAR" 2>/dev/null | tr -d ' ')
if [ "$CURRENT" = "0000" ] && [ -f "$STAMP" ]; then
    exit 0
fi

# efivarfs marks variables immutable by default; remove flag before writing
chattr -i "$EFIVAR" 2>/dev/null || true

# EFI variable layout: 4-byte attributes (LE) + variable data
# Attributes: NV(0x01)|BS(0x02)|RT(0x04) = 0x07; Timeout = UINT16 0x0000
printf '\x07\x00\x00\x00\x00\x00' > "$EFIVAR" && \
    echo "efi-timeout: boot timeout set to 0" && \
    touch "$STAMP"
