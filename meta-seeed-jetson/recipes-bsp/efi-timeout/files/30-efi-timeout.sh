#!/bin/sh
# init-extra.d hook — write UEFI Timeout=0 from inside the flashing initrd.
# L4T stores EFI vars in QSPI, so this persists across reboots and rootfs
# reflashes. Eliminates the need for the per-boot efi-timeout.service on
# the rootfs.
set -eu

EFIVARS=/sys/firmware/efi/efivars
EFIVAR="$EFIVARS/Timeout-8be4df61-93ca-11d2-aa0d-00e098032b8c"

echo "30-efi-timeout: starting"

# efivarfs may not be mounted yet inside the flash initrd
if ! mountpoint -q "$EFIVARS" 2>/dev/null; then
    mkdir -p "$EFIVARS"
    mount -t efivarfs efivarfs "$EFIVARS" 2>/dev/null || {
        echo "30-efi-timeout: efivarfs not available, skipping" >&2
        exit 0
    }
fi

if ! [ -f "$EFIVAR" ]; then
    # Variable doesn't exist yet — we can still create it by writing to
    # the path. efivarfs supports CREATE on first write.
    :
else
    # If already zero, no work
    CURRENT=$(od -An -tx1 -N2 -j4 "$EFIVAR" 2>/dev/null | tr -d ' ' || true)
    if [ "$CURRENT" = "0000" ]; then
        echo "30-efi-timeout: already 0, nothing to do"
        exit 0
    fi
    # Drop immutable flag so we can overwrite
    chattr -i "$EFIVAR" 2>/dev/null || true
fi

# EFI variable layout: 4-byte attributes (LE) NV|BS|RT = 0x07; UINT16 = 0
printf '\x07\x00\x00\x00\x00\x00' > "$EFIVAR" && \
    echo "30-efi-timeout: boot timeout set to 0"
