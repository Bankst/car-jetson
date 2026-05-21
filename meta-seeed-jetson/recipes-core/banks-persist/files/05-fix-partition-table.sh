#!/bin/sh
# init-extra.d hook — early (lex 05) partition-table rescan.
# Modeled on MC2's same-named script. When re-flashing over a prior install
# the kernel may have the old partition table cached; without forcing a
# rescan, /dev/nvme0n1pN nodes either don't appear or point at wrong sectors.
LOGS_DIR="/tmp/flashpkg/flashpkg/logs"
LOG="$LOGS_DIR/05-fix-partition-table.log"

mkdir -p "$LOGS_DIR"
echo "05-fix-partition-table: starting" | tee -a "$LOG"

if [ ! -b /dev/nvme0n1 ]; then
    echo "05-fix-partition-table: /dev/nvme0n1 missing, skipping" | tee -a "$LOG"
    exit 0
fi

echo "Partition nodes BEFORE rescan:" | tee -a "$LOG"
ls -1 /dev/nvme0n1p* 2>/dev/null | tee -a "$LOG" || echo "  (none)" | tee -a "$LOG"

# partprobe is in this initrd; blockdev typically isn't.
if command -v partprobe >/dev/null 2>&1; then
    partprobe /dev/nvme0n1 >> "$LOG" 2>&1
    echo "partprobe exit: $?" | tee -a "$LOG"
fi

# Give udev/kernel a moment to enumerate.
sleep 2

echo "Partition nodes AFTER rescan:" | tee -a "$LOG"
ls -1 /dev/nvme0n1p* 2>/dev/null | tee -a "$LOG" || echo "  (none)" | tee -a "$LOG"

if command -v sgdisk >/dev/null 2>&1; then
    echo "GPT layout:" | tee -a "$LOG"
    sgdisk /dev/nvme0n1 --print >> "$LOG" 2>&1
fi

echo "05-fix-partition-table: done" | tee -a "$LOG"
