#!/bin/sh
# banks-audio-route — establish HW audio path through AHUB accelerators.
#
# Default route on devkit:
#   PipeWire (ALSA) -> ADMAIF1 -> MVC1 -> OPE1 -> I2S5 -> PCM5102A
#
# MVC1 = HW-ramped master volume (linear curve: int = dB*100 + 12000;
#        unity = 12000, default 11500 = -5 dB system headroom).
# OPE1 = PEQ (parametric EQ, 12 biquad stages x 8 channels) + MBDRC
#        (multi-band DRC). PEQ defaults to inactive — coefficients must
#        be uploaded before enabling, otherwise output is silenced.
#
# SPKPROT1 is intentionally NOT routed through. On L4T R35.6.4 it is a
# dead routing slot: no kernel driver binds the AHUB-side compatible,
# and the matching ADSP plugin (nvspkprot.elf) is not in the static
# adsp-fw.bin. See docs/speaker-protection-options.md.
#
# Overridable from /etc/banks-audio/route.conf.

set -eu

CONF=/etc/banks-audio/route.conf
[ -r "$CONF" ] && . "$CONF"

CARD="${BANKS_AUDIO_CARD:-APE}"
SINK_I2S="${BANKS_AUDIO_SINK_I2S:-I2S5}"
SOURCE_ADMAIF="${BANKS_AUDIO_SOURCE_ADMAIF:-ADMAIF1}"
# PEQ_STAGES is N-1 encoded by the driver: 11 = 12 active biquad stages
# (silicon maximum). The kernel control accepts min=0 max=11.
PEQ_STAGES="${BANKS_AUDIO_PEQ_STAGES:-11}"
PEQ_DEFAULT_ACTIVE="${BANKS_AUDIO_PEQ_ACTIVE:-off}"
MBDRC_MODE="${BANKS_AUDIO_MBDRC_MODE:-bypass}"
I2S5_RATE="${BANKS_AUDIO_I2S5_RATE:-48000}"
I2S5_CHANNELS="${BANKS_AUDIO_I2S5_CHANNELS:-2}"
I2S5_BITS="${BANKS_AUDIO_I2S5_BITS:-16}"
# Linear curve: int = dB*100 + 12000. 11500 = -5 dB headroom for hot content.
# 12000 = 0 dB unity. Range 0..16000 (-120 dB .. +40 dB).
MASTER_VOL="${BANKS_AUDIO_MASTER_VOL:-11500}"

log() { echo "banks-audio-route: $*"; }

wait_for_card() {
  i=0
  while [ $i -lt 20 ]; do
    [ -d "/proc/asound/$CARD" ] && return 0
    sleep 0.25
    i=$((i + 1))
  done
  log "ERROR: card $CARD never appeared"
  return 1
}

cset() {
  name="$1"; shift
  amixer -c "$CARD" cset name="$name" "$@" >/dev/null 2>&1 \
    || { log "WARN: cset '$name' '$*' failed"; return 1; }
}

wait_for_card

log "wiring playback route: $SOURCE_ADMAIF -> MVC1 -> OPE1 -> $SINK_I2S"

cset "MVC1 Mux"        "$SOURCE_ADMAIF"
cset "OPE1 Mux"        "MVC1"
cset "${SINK_I2S} Mux" "OPE1"

log "I2S5 format: ${I2S5_RATE} Hz, ${I2S5_CHANNELS} ch, ${I2S5_BITS}-bit"
cset "${SINK_I2S} Sample Rate"             "$I2S5_RATE"
cset "${SINK_I2S} Playback Audio Channels" "$I2S5_CHANNELS"
cset "${SINK_I2S} Playback Audio Bit Format" "$I2S5_BITS"

log "MVC1 master volume init = $MASTER_VOL ($(( (MASTER_VOL - 12000) / 100 )) dB nominal)"
cset "MVC1 Volume" "$MASTER_VOL"
cset "MVC1 Mute"   "off"
cset "MVC1 Bypass" "off"

log "OPE1 PEQ stages=$PEQ_STAGES (N-1 encoded; 11 -> 12 active) active=$PEQ_DEFAULT_ACTIVE; MBDRC mode=$MBDRC_MODE"
cset "OPE1 PEQ Biquad Stages" "$PEQ_STAGES"
cset "OPE1 PEQ Active"        "$PEQ_DEFAULT_ACTIVE"
cset "OPE1 MBDRC Mode"        "$MBDRC_MODE"

# Capture-side: leave ADMAIF1 Mux pointing at I2S5 (loopback/capture default).
# Tools using `arecord` from card APE,0 will still see I2S5 input.
cset "ADMAIF1 Mux" "$SINK_I2S" || true

log "route established"
