#!/bin/bash
# Banks live EQ tester — loops pink noise through a param_eq filter-chain,
# keys adjust per-band gains live. 8 bands stereo via single param_eq node.
# Target: jetson devkit, system-instance PipeWire 1.0.9.
set -uo pipefail

PW_CONF_DIR=/etc/pipewire/pipewire.conf.d
EQ_CONF="$PW_CONF_DIR/99-banks-eq.conf"
SINK_NAME="banks_eq"
NOISE_GEN=/tmp/banks-noise-gen.py
KEEP_CONF=0        # 1 = leave config installed on exit (for app-side testing)

for t in wpctl pw-cli pw-cat python3 systemctl; do command -v "$t" >/dev/null || { echo "missing $t" >&2; exit 1; }; done

PREV_DEFAULT=$(wpctl inspect @DEFAULT_AUDIO_SINK@ 2>/dev/null | awk -F'"' '/node\.name/{print $2; exit}')
PREV_DEFAULT_DESC=$(wpctl inspect @DEFAULT_AUDIO_SINK@ 2>/dev/null | awk -F'"' '/node\.description/{print $2; exit}')

# 8 bands: low shelf, 6 peaks, high shelf. Centre frequencies (Hz) and Q.
BAND_FREQS=(60 150 400 1000 2500 6000 10000 15000)
BAND_QS=(0.7 1.0 1.0 1.0 1.0 1.0 1.0 0.7)
BAND_TYPES=(bq_lowshelf bq_peaking bq_peaking bq_peaking bq_peaking bq_peaking bq_peaking bq_highshelf)
BAND_LABELS=("60Hz LS" "150Hz" "400Hz" "1kHz" "2.5kHz" "6kHz" "10kHz" "15kHz HS")

# Live gain state (dB), one per band, plus a snapshot of "applied" gains for ramping.
GAINS=(0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0)
APPLIED=(0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0)
BYPASS=0

# Streaming pink noise generator. Writes WAV header with max data size, then
# generates samples to stdout forever until SIGPIPE.
render_pink_gen() {
  cat > "$NOISE_GEN" <<'PY'
import math, struct, sys, random
SR = 48000; CH = 2; BPS = 16
BR = SR*CH*BPS//8; BA = CH*BPS//8
DS = 0x7FFFFF00
hdr = (b"RIFF" + struct.pack("<I", 36+DS) + b"WAVE"
       + b"fmt " + struct.pack("<IHHIIHH", 16, 1, CH, SR, BR, BA, BPS)
       + b"data" + struct.pack("<I", DS))
sys.stdout.buffer.write(hdr); sys.stdout.buffer.flush()
b0=b1=b2=b3=b4=b5=b6=0.0
def pink(w):
    global b0,b1,b2,b3,b4,b5,b6
    b0 = 0.99886*b0 + w*0.0555179
    b1 = 0.99332*b1 + w*0.0750759
    b2 = 0.96900*b2 + w*0.1538520
    b3 = 0.86650*b3 + w*0.3104856
    b4 = 0.55000*b4 + w*0.5329522
    b5 = -0.7616*b5 - w*0.0168980
    p  = b0+b1+b2+b3+b4+b5+b6+w*0.5362
    b6 = w*0.115926
    return p * 0.11
CHUNK = 2048
try:
    while True:
        buf = bytearray()
        for _ in range(CHUNK):
            wl = random.uniform(-1,1); pl = pink(wl)
            wr = random.uniform(-1,1); pr = pink(wr)
            sl  = max(-32767, min(32767, int(pl*32767)))
            sr_ = max(-32767, min(32767, int(pr*32767)))
            buf += struct.pack("<hh", sl, sr_)
        sys.stdout.buffer.write(buf); sys.stdout.buffer.flush()
except (BrokenPipeError, KeyboardInterrupt):
    pass
PY
}

ensure_config() {
  if [[ -f "$EQ_CONF" ]] && grep -q "param_eq" "$EQ_CONF" 2>/dev/null && grep -q "$SINK_NAME" "$EQ_CONF"; then
    echo "config already present at $EQ_CONF (param_eq form)"
    return 0
  fi
  mkdir -p "$PW_CONF_DIR"
  # Build the filters = [ ... ] block from the band arrays.
  local filters=""
  local i
  for i in 0 1 2 3 4 5 6 7; do
    filters+="            { type = ${BAND_TYPES[$i]} freq = ${BAND_FREQS[$i]} gain = 0.0 q = ${BAND_QS[$i]} }"$'\n'
  done
  cat > "$EQ_CONF" <<EOF
context.modules = [
  { name = libpipewire-module-filter-chain
    args = {
      node.description = "Banks EQ"
      media.name       = "Banks EQ"
      filter.graph = {
        nodes = [
          { type = builtin name = eq label = param_eq
            config = {
              filters = [
$filters            ]
            }
          }
        ]
        inputs  = [ "eq:In 1" "eq:In 2" ]
        outputs = [ "eq:Out 1" "eq:Out 2" ]
      }
      capture.props  = {
        node.name      = "$SINK_NAME"
        media.class    = "Audio/Sink"
        audio.channels = 2
        audio.position = [ FL FR ]
      }
      playback.props = {
        node.name      = "${SINK_NAME}_out"
        node.passive   = true
        audio.channels = 2
        audio.position = [ FL FR ]
      }
    }
  }
]
EOF
  echo "wrote $EQ_CONF, restarting pipewire..."
  systemctl restart pipewire wireplumber
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 1
    if systemctl is-active --quiet pipewire && wpctl status >/dev/null 2>&1; then return 0; fi
  done
  echo "pipewire didn't recover; abort" >&2
  return 1
}

find_eq_id() {
  EQ_ID=$(wpctl status | awk -v s="$SINK_NAME" '
    $0 ~ s {
      for(i=1;i<=NF;i++) if(match($i,/^[0-9]+\.$/)){gsub(/\./,"",$i); print $i; exit}
    }')
  [[ -n "$EQ_ID" ]]
}

# Write the current bypass-or-gain set to PipeWire in one Props pod.
# param_eq exposes per-band controls as "Gain 1", "Gain 2", ..., "Gain 8".
write_gains_now() {
  local g1 g2 g3 g4 g5 g6 g7 g8
  if (( BYPASS )); then
    g1=0.0; g2=0.0; g3=0.0; g4=0.0; g5=0.0; g6=0.0; g7=0.0; g8=0.0
  else
    g1=${APPLIED[0]}; g2=${APPLIED[1]}; g3=${APPLIED[2]}; g4=${APPLIED[3]}
    g5=${APPLIED[4]}; g6=${APPLIED[5]}; g7=${APPLIED[6]}; g8=${APPLIED[7]}
  fi
  pw-cli s "$EQ_ID" Props \
    "{ params = [ \"eq:Gain 1\" $g1  \"eq:Gain 2\" $g2  \"eq:Gain 3\" $g3  \"eq:Gain 4\" $g4 \
                  \"eq:Gain 5\" $g5  \"eq:Gain 6\" $g6  \"eq:Gain 7\" $g7  \"eq:Gain 8\" $g8 ] }" \
    >/dev/null 2>&1
}

# Ramp APPLIED -> GAINS in 0.25 dB / 30 ms steps to dodge biquad_set() history-reset clicks.
# Operates on all bands in parallel; finishes when every band has reached its target.
ramp_to_targets() {
  if (( BYPASS )); then
    # Bypass: jump straight (biquad already silent path-wise).
    write_gains_now
    return
  fi
  local step=0.25
  local done_all=0
  while (( ! done_all )); do
    done_all=1
    local i
    for i in 0 1 2 3 4 5 6 7; do
      local cur="${APPLIED[$i]}"
      local tgt="${GAINS[$i]}"
      # diff = tgt - cur; if |diff| <= step: snap to tgt; else step toward tgt.
      local next
      next=$(awk -v c="$cur" -v t="$tgt" -v s="$step" 'BEGIN{
        d=t-c;
        ad=(d<0)?-d:d;
        if(ad<=s){printf "%.2f", t}
        else if(d>0){printf "%.2f", c+s}
        else{printf "%.2f", c-s}
      }')
      APPLIED[$i]="$next"
      if [[ "$next" != "$tgt" ]]; then done_all=0; fi
    done
    write_gains_now
    # 30 ms inter-step pause. Use printf 0 read trick? bash builtin sleep needs coreutils;
    # ours has /bin/sleep supporting fractional seconds.
    sleep 0.03
  done
}

bar() {
  local g="$1"
  awk -v g="$g" 'BEGIN{
    n=int(g+12+0.5); if(n<0)n=0; if(n>24)n=24;
    for(i=0;i<24;i++){
      if(i==12){printf "|"}
      else if((g>=0 && i>=12 && i<12+n-12) || (g<0 && i>=12+n-12 && i<12)){printf "#"}
      else{printf "-"}
    }
  }'
}

ui() {
  clear
  cat <<EOF
== Banks live EQ (param_eq, 8 bands, ramped) ==
  source: pink noise loop  (sink: banks_eq -> $PREV_DEFAULT_DESC)
  bypass: $([[ $BYPASS -eq 1 ]] && echo "ON  (signal flat)" || echo "off (EQ active)")

EOF
  local i
  for i in 0 1 2 3 4 5 6 7; do
    printf "  [%d] %-10s  %+5.1f dB  [%s]\n" \
      "$((i+1))" "${BAND_LABELS[$i]}" "${GAINS[$i]}" "$(bar "${GAINS[$i]}")"
  done
  cat <<'EOF'

  band-select : 1..8 picks the active band
  current band gain:
        -/_   -1 dB        +/=   +1 dB
        [     -3 dB        ]     +3 dB
        \     zero this band

  presets : F1 flat   F2 bass+   F3 voice   F4 treble+   F5 V-shape
            (or shifted digits: ! @ # $ %)
  space : bypass toggle      0 : zero ALL bands
  q / Q / Ctrl-C : quit
EOF
  printf "\n  active band: [%d] %s\n" "$((SEL+1))" "${BAND_LABELS[$SEL]}"
}

clamp() {
  awk -v v="$1" 'BEGIN{ if(v>12)v=12; if(v<-12)v=-12; printf "%.1f", v }'
}

bump_sel() {
  # $1 = signed delta (dB), applied to the currently selected band.
  local cur new
  cur=${GAINS[$SEL]}
  new=$(awk -v c="$cur" -v d="$1" 'BEGIN{printf "%.1f", c+d}')
  new=$(clamp "$new")
  GAINS[$SEL]=$new
}

preset() {
  # 8-band presets — make them noticeably distinct.
  # Bands: 60 LS, 150, 400, 1k, 2.5k, 6k, 10k, 15k HS
  case "$1" in
    flat)    GAINS=( 0.0  0.0  0.0  0.0  0.0  0.0  0.0  0.0) ;;
    bass)    GAINS=( 6.0  4.0  1.0  0.0 -1.0 -1.0  0.0  1.0) ;;
    voice)   GAINS=(-4.0 -2.0  1.0  4.0  4.0  2.0  0.0 -2.0) ;;
    treble)  GAINS=(-1.0 -1.0  0.0  0.0  1.0  3.0  5.0  6.0) ;;
    vshape)  GAINS=( 6.0  4.0  1.0 -3.0 -4.0 -1.0  3.0  6.0) ;;
  esac
  BYPASS=0
}

# Background streaming player.
NOISE_PID=0
start_noise() {
  setsid bash -c "python3 '$NOISE_GEN' | pw-cat -p - >/dev/null 2>&1" &
  NOISE_PID=$!
}
stop_noise() {
  if [[ $NOISE_PID -gt 0 ]]; then
    kill -TERM -- "-$NOISE_PID" 2>/dev/null || kill "$NOISE_PID" 2>/dev/null
    wait "$NOISE_PID" 2>/dev/null || true
    NOISE_PID=0
  fi
}

cleanup() {
  stty sane 2>/dev/null || true
  echo
  echo "cleaning up..."
  stop_noise
  if (( ! KEEP_CONF )); then
    rm -f "$EQ_CONF"
    systemctl restart pipewire wireplumber
    echo "config removed, pipewire restarted"
    sleep 2
    if [[ -n "$PREV_DEFAULT_DESC" ]]; then
      local id
      id=$(wpctl status | awk -v d="$PREV_DEFAULT_DESC" '
        BEGIN{want=0}
        /Audio/{want=1} /Video/{want=0}
        want && /Sinks:/{insinks=1; next}
        want && insinks && index($0, d){
          for(i=1;i<=NF;i++) if(match($i,/^[0-9]+\.$/)){gsub(/\./,"",$i); print $i; exit}
        }')
      if [[ -n "$id" ]]; then
        wpctl set-default "$id" && echo "default -> $PREV_DEFAULT_DESC (id=$id)"
      else
        echo "couldn't find '$PREV_DEFAULT_DESC' post-restart; pick manually:"
        wpctl status | sed -n '/Sinks:/,/Sources:/p'
      fi
    fi
  else
    echo "config left at $EQ_CONF"
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

render_pink_gen
ensure_config || exit 1
find_eq_id || { echo "filter-chain node missing"; exit 1; }
wpctl set-default "$EQ_ID"
echo "EQ node id=$EQ_ID set as default"

# Active band selector (0..7).
SEL=3   # default cursor on 1 kHz

# Initial write — already flat, but pushes APPLIED state to PipeWire.
write_gains_now
start_noise

echo
echo "press any key to start interactive loop (or Ctrl-C to abort)..."
IFS= read -rsn1 _start

# Interactive loop. Short read timeout so SIGINT / Ctrl-C lands fast.
while :; do
  ui
  if ! IFS= read -rsn1 -t 1 key; then
    continue
  fi
  case "$key" in
    q|Q) break ;;

    # band selection
    1) SEL=0 ;;
    2) SEL=1 ;;
    3) SEL=2 ;;
    4) SEL=3 ;;
    5) SEL=4 ;;
    6) SEL=5 ;;
    7) SEL=6 ;;
    8) SEL=7 ;;

    # gain on selected band
    '-'|'_')  bump_sel -1 ;;
    '+'|'=')  bump_sel +1 ;;
    '[')      bump_sel -3 ;;
    ']')      bump_sel +3 ;;
    '\')      GAINS[$SEL]=0.0 ;;

    # presets — shifted digits, easy reach from band-select row
    '!') preset flat ;;
    '@') preset bass ;;
    '#') preset voice ;;
    '$') preset treble ;;
    '%') preset vshape ;;

    ' ') BYPASS=$((1 - BYPASS)) ;;
    '0') GAINS=(0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0); BYPASS=0 ;;
    *) continue ;;
  esac
  ramp_to_targets
done
