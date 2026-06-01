#!/bin/sh
# banks-audio-route — establish HW audio path through AHUB accelerators.
#
# Default route on devkit:
#   PipeWire (ALSA) -> ADMAIF1 -> MVC1 -> OPE1 -> I2S5 -> PCM5102A
#
# MVC1   = master volume (HW-ramped, zero-click).
# OPE1   = PEQ (parametric EQ) + MBDRC (multi-band DRC).
# SPKPROT1 (optional) = speaker protection — set INSERT_SPKPROT=1 to enable.
#
# Overridable from /etc/banks-audio/route.conf.

set -eu

CONF=/etc/banks-audio/route.conf
[ -r "$CONF" ] && . "$CONF"

CARD="${BANKS_AUDIO_CARD:-APE}"
SINK_I2S="${BANKS_AUDIO_SINK_I2S:-I2S5}"
SOURCE_ADMAIF="${BANKS_AUDIO_SOURCE_ADMAIF:-ADMAIF1}"
INSERT_SPKPROT="${BANKS_AUDIO_INSERT_SPKPROT:-0}"
PEQ_STAGES="${BANKS_AUDIO_PEQ_STAGES:-8}"
PEQ_DEFAULT_ACTIVE="${BANKS_AUDIO_PEQ_ACTIVE:-off}"
MBDRC_MODE="${BANKS_AUDIO_MBDRC_MODE:-bypass}"
I2S5_RATE="${BANKS_AUDIO_I2S5_RATE:-48000}"
I2S5_CHANNELS="${BANKS_AUDIO_I2S5_CHANNELS:-2}"
I2S5_BITS="${BANKS_AUDIO_I2S5_BITS:-16}"
# MVC1 Volume range is 0..16000 — empirically near unity around 12000. Set
# high by default so inserting MVC into a previously-bypassed path doesn't
# silence everything. Calibrate against actual SPL once amp + speakers known.
MASTER_VOL="${BANKS_AUDIO_MASTER_VOL:-12000}"

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

log "wiring playback route: $SOURCE_ADMAIF -> MVC1 -> OPE1$( [ "$INSERT_SPKPROT" = 1 ] && echo ' -> SPKPROT1') -> $SINK_I2S"

cset "MVC1 Mux"      "$SOURCE_ADMAIF"

if [ "$INSERT_SPKPROT" = 1 ]; then
  cset "OPE1 Mux"      "MVC1"
  cset "SPKPROT1 Mux"  "OPE1"
  cset "${SINK_I2S} Mux" "SPKPROT1"
else
  cset "OPE1 Mux"      "MVC1"
  cset "${SINK_I2S} Mux" "OPE1"
fi

log "I2S5 format: ${I2S5_RATE} Hz, ${I2S5_CHANNELS} ch, ${I2S5_BITS}-bit"
cset "${SINK_I2S} Sample Rate"             "$I2S5_RATE"
cset "${SINK_I2S} Playback Audio Channels" "$I2S5_CHANNELS"
cset "${SINK_I2S} Playback Audio Bit Format" "$I2S5_BITS"

log "MVC1 master volume init = $MASTER_VOL"
cset "MVC1 Volume" "$MASTER_VOL"
cset "MVC1 Mute"   "off"
cset "MVC1 Bypass" "off"

log "OPE1 PEQ stages=$PEQ_STAGES active=$PEQ_DEFAULT_ACTIVE; MBDRC mode=$MBDRC_MODE"
cset "OPE1 PEQ Biquad Stages" "$PEQ_STAGES"
cset "OPE1 PEQ Active"        "$PEQ_DEFAULT_ACTIVE"
cset "OPE1 MBDRC Mode"        "$MBDRC_MODE"

# Capture-side: leave ADMAIF1 Mux pointing at I2S5 (loopback/capture default).
# Tools using `arecord` from card APE,0 will still see I2S5 input.
cset "ADMAIF1 Mux" "$SINK_I2S" || true

log "route established"
