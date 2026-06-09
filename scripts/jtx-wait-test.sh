#!/bin/bash
# Wait for Jetson to boot (USB-NCM), load cs42xx8 codec, set route, play test tone.
# Usage: ./scripts/jtx-wait-test.sh
# Runs until Jetson is reachable, then auto-tests audio.

set -euo pipefail

TARGET="${JTX_TARGET:-root@192.168.55.1}"
SSH="/usr/bin/ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5"

echo ">>> Waiting for Jetson at 192.168.55.1..."
until ping -c 1 -W 1 192.168.55.1 >/dev/null 2>&1; do sleep 2; done
echo ">>> Jetson pingable, waiting for SSH..."
sleep 3
until $SSH $TARGET 'true' 2>/dev/null; do sleep 2; done
echo ">>> SSH up"

echo ">>> Loading cs42xx8 I2C module..."
$SSH $TARGET 'modprobe snd_soc_cs42xx8_i2c' 2>&1

echo ">>> Waiting for APE card..."
$SSH $TARGET 'for i in $(seq 1 30); do [ -d /proc/asound/APE ] && break; sleep 1; done'

echo ">>> Setting AHUB route: ADMAIF1 -> I2S5"
$SSH $TARGET '
amixer -c 1 cset name="I2S5 Mux" "ADMAIF1" >/dev/null
amixer -c 1 cset name="CS42448 DAC1 Playback Volume" 220 >/dev/null
amixer -c 1 cset name="CS42448 DAC2 Playback Volume" 220 >/dev/null
amixer -c 1 cset name="CS42448 DAC Auto Mute Switch" 0 >/dev/null
amixer -c 1 cset name="ADMAIF1 Mux" "I2S5" >/dev/null
' 2>&1

echo ">>> Playing 440Hz test tone..."
$SSH $TARGET 'speaker-test -c 2 -t sine -f 440 -D hw:1,0 -l 2' 2>&1 | tail -8

echo ">>> Audio dmesg:"
$SSH $TARGET 'dmesg | grep -iE "set_dai_sysclk|unsupported sysclk|PRE_PMU.*failed|found device|ratio" | grep -v "Modules linked" | tail -10' 2>&1

echo ">>> Done."
