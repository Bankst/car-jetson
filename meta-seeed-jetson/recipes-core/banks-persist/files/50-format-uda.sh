#!/bin/sh
# init-extra.d hook — formats UDA partition as ext4 (if not already) and seeds
# layout. Runs inside the flashing initrd after 05-fix-partition-table has
# forced a partition rescan. UDA is partition 15 in the standard NVIDIA L4T
# layout; we hard-code the path because /dev/disk/by-partlabel/ requires udev
# which isn't running in this initrd.
set -u

LOGS_DIR="/tmp/flashpkg/flashpkg/logs"
LOG="$LOGS_DIR/50-format-uda.log"
DEV=/dev/nvme0n1p15
MNT=/tmp/uda-mnt

mkdir -p "$LOGS_DIR"
echo "50-format-uda: starting" | tee -a "$LOG"

if [ ! -b "$DEV" ]; then
    echo "50-format-uda: $DEV missing, skipping" | tee -a "$LOG"
    exit 0
fi

# Verify GPT partition name matches UDA (defence-in-depth).
if command -v sgdisk >/dev/null 2>&1; then
    PNAME=$(sgdisk /dev/nvme0n1 -i 15 2>/dev/null | awk -F"'" '/Partition name/{print $2}')
    if [ "$PNAME" != "UDA" ]; then
        echo "50-format-uda: partition 15 name is '$PNAME', expected 'UDA' — aborting" | tee -a "$LOG"
        exit 0
    fi
fi

FSTYPE=$(blkid -o value -s TYPE "$DEV" 2>/dev/null || true)
echo "50-format-uda: $DEV current fstype='$FSTYPE'" | tee -a "$LOG"

if [ "$FSTYPE" != "ext4" ]; then
    echo "50-format-uda: formatting $DEV as ext4" | tee -a "$LOG"
    mkfs.ext4 -F -q -L banks-data "$DEV" 2>&1 | tee -a "$LOG"
fi

mkdir -p "$MNT"
if ! mount "$DEV" "$MNT" 2>&1 | tee -a "$LOG"; then
    echo "50-format-uda: mount failed" | tee -a "$LOG"
    exit 1
fi

mkdir -p "$MNT/bluetooth" "$MNT/ssh" "$MNT/config"
touch "$MNT/.banks-persist-initialized"
sync

umount "$MNT" 2>&1 | tee -a "$LOG"
rmdir "$MNT" 2>/dev/null || true

echo "50-format-uda: done" | tee -a "$LOG"
