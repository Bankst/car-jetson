#!/usr/bin/env bash
# push-cs42448.sh — deploy the CS42448 SoC-audio bring-up to a running A203
# without a full recovery-mode flash. Pushes the Tegra ASoC module set + the
# cs42xx8 codec driver + the rebuilt DTB (with the audio-codec@48 node), then
# reboots and probes.
#
# Safe to re-run. Aborts if any gate fails rather than half-deploying.
#
# Gates checked before touching the board:
#   1. board reachable
#   2. running `uname -r` == the kver these modules were built for (vermagic)
#   3. extlinux FDT actually points at a rootfs DTB we can overwrite
#      (if the bootloader loads a flashed kernel-dtb partition instead, a
#       module push can't update the DT -> full flash required; we abort)
set -euo pipefail

TARGET="${JTX_TARGET:-root@192.168.55.1}"
K="5.10.216-l4t-r35.6.4+g050b88cc9d0f"
BUILD="build/tmp/work/jetson_xavier_nx_a203-poky-linux"
MODDIR="$BUILD/linux-tegra/5.10.216+git/image/usr/lib/modules/$K/kernel/sound/soc"
DTB="$BUILD/seeed-a203-devicetree/1.0/build/tegra194-p3668-a203.dtb"

say() { printf '\n=== %s ===\n' "$*"; }
die() { printf 'push-cs42448: %s\n' "$*" >&2; exit 1; }

[ -d "$MODDIR" ] || die "module dir not found ($MODDIR) — run the build first"
[ -f "$DTB" ]    || die "DTB not found ($DTB) — run the build first"

say "Gate 1: board reachable"
ssh -o ConnectTimeout=5 "$TARGET" true 2>/dev/null || die "board unreachable at $TARGET — connect USB and retry"

say "Gate 2: kernel vermagic match"
RK=$(ssh "$TARGET" 'uname -r')
echo "  board: $RK"
echo "  build: $K"
[ "$RK" = "$K" ] || die "kver mismatch — modules won't load; full flash required"

say "Gate 3: DTB load path (extlinux FDT)"
FDT=$(ssh "$TARGET" 'grep -iE "^\s*FDT" /boot/extlinux/extlinux.conf 2>/dev/null | head -1')
echo "  extlinux: ${FDT:-<none>}"
echo "$FDT" | grep -q '/boot/' || die "extlinux FDT does not point at a rootfs /boot DTB — DT is flashed; full flash required"
FDTPATH=$(echo "$FDT" | awk '{print $2}')

say "Push: Tegra ASoC modules + cs42xx8 codec"
DST="/lib/modules/$K/kernel/sound/soc"
ssh "$TARGET" "mkdir -p $DST/tegra $DST/codecs"
scp "$MODDIR"/tegra/*.ko        "$TARGET:$DST/tegra/"
scp "$MODDIR"/codecs/snd-soc-cs42xx8*.ko "$TARGET:$DST/codecs/"
# tegra210-adma lives under drivers/dma, not sound/soc — push if present
ADMA="$BUILD/linux-tegra/5.10.216+git/image/usr/lib/modules/$K/kernel/drivers/dma/tegra210-adma.ko"
[ -f "$ADMA" ] && { ssh "$TARGET" "mkdir -p /lib/modules/$K/kernel/drivers/dma"; scp "$ADMA" "$TARGET:/lib/modules/$K/kernel/drivers/dma/"; }

say "depmod on target"
ssh "$TARGET" "depmod -a $K"

say "Push: rebuilt DTB -> $FDTPATH"
ssh "$TARGET" "cp -a '$FDTPATH' '${FDTPATH}.bak.precs42448' 2>/dev/null || true"
scp "$DTB" "$TARGET:$FDTPATH"

say "Reboot"
ssh "$TARGET" 'systemctl reboot' || true
echo "  waiting for board to come back..."
sleep 45
for i in $(seq 1 30); do ssh -o ConnectTimeout=5 "$TARGET" true 2>/dev/null && break; sleep 5; done

say "Probe: cs42xx8 driver bind"
ssh "$TARGET" 'dmesg | grep -iE "cs42xx8|cs42448" || echo "  (no cs42xx8 dmesg — check probe)"'
say "Probe: ALSA cards"
ssh "$TARGET" 'cat /proc/asound/cards; echo; aplay -l 2>/dev/null | grep -iE "card|CS42448" || true'
echo
echo "push-cs42448: done. If no card, check: dmesg | grep -iE 'tegra.*ape|admaif|i2s5'"
