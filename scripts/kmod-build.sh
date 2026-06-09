#!/bin/bash
# Cross-compile a single kernel module and push to Jetson.
# Usage: ./scripts/kmod-build.sh sound/soc/codecs [snd-soc-cs42xx8]
#
# First arg:  M= path (relative to kernel source root)
# Second arg: optional module name to push (without .ko); if omitted, pushes all .ko in M=

set -euo pipefail

PROJDIR="$(cd "$(dirname "$0")/.." && pwd)"
KVER="5.10.216-l4t-r35.6.4+g050b88cc9d0f"
KSRC="$PROJDIR/build/tmp/work-shared/jetson-xavier-nx-a203/kernel-source"
KBUILD="$PROJDIR/build/tmp/work/jetson_xavier_nx_a203-poky-linux/linux-tegra/5.10.216+git/linux-jetson_xavier_nx_a203-standard-build"
CROSS="${CROSS_COMPILE:-aarch64-linux-gnu-}"
TARGET="${JTX_TARGET:-root@192.168.55.1}"
MODDIR="/lib/modules/$KVER/kernel"

M="${1:?Usage: $0 <M=path> [module-name]}"
MOD="${2:-}"

# One-time: rebuild uninative host tools (fixdep, modpost) as native x86_64
FIXDEP="$KBUILD/scripts/basic/fixdep"
MODPOST="$KBUILD/scripts/mod/modpost"
if file "$FIXDEP" 2>/dev/null | grep -q 'sysroots-uninative'; then
    echo ">>> Rebuilding fixdep natively..."
    gcc -o "$FIXDEP" "$KSRC/scripts/basic/fixdep.c"
fi
if file "$MODPOST" 2>/dev/null | grep -q 'sysroots-uninative'; then
    echo ">>> Rebuilding modpost natively..."
    gcc -o "$MODPOST" \
        -I"$KBUILD/scripts/mod" \
        -I"$KSRC/scripts/mod" \
        "$KSRC/scripts/mod/modpost.c" \
        "$KSRC/scripts/mod/file2alias.c" \
        "$KSRC/scripts/mod/sumversion.c" \
        -lelf
fi

echo ">>> Building M=$M"
make -C "$KSRC" O="$KBUILD" CROSS_COMPILE="$CROSS" ARCH=arm64 M="$M" modules -j$(nproc)

if [ -n "$MOD" ]; then
    KO="$KBUILD/$M/$MOD.ko"
    [ -f "$KO" ] || { echo "ERROR: $KO not found"; exit 1; }
    echo ">>> Pushing $MOD.ko to $TARGET"
    cat "$KO" | ssh -o StrictHostKeyChecking=no "$TARGET" \
        "cat > /tmp/$MOD.ko && cp /tmp/$MOD.ko $MODDIR/$M/$MOD.ko && echo INSTALLED"
else
    for KO in "$KBUILD/$M"/*.ko; do
        [ -f "$KO" ] || continue
        NAME="$(basename "$KO")"
        echo ">>> Pushing $NAME to $TARGET"
        cat "$KO" | ssh -o StrictHostKeyChecking=no "$TARGET" \
            "cat > /tmp/$NAME && cp /tmp/$NAME $MODDIR/$M/$NAME && echo INSTALLED: $NAME"
    done
fi

echo ">>> Done. Reload on target: modprobe -r <mod> && modprobe <mod>"
