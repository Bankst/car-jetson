#!/bin/bash
# Banks live EQ tester — loops pink noise through filter-chain, keys adjust gains.
# Target: jetson devkit, system-instance PipeWire.
set -uo pipefail

PW_CONF_DIR=/etc/pipewire/pipewire.conf.d
EQ_CONF="$PW_CONF_DIR/99-banks-eq.conf"
SINK_NAME="banks_eq"
NOISE_GEN=/tmp/banks-noise-gen.py
KEEP_CONF=0        # 1 = leave config installed on exit (for app-side testing)

for t in wpctl pw-cli pw-cat python3 systemctl; do command -v "$t" >/dev/null || { echo "missing $t" >&2; exit 1; }; done

PREV_DEFAULT=$(wpctl inspect @DEFAULT_AUDIO_SINK@ | awk -F'"' '/node\.name/{print $2; exit}')
PREV_DEFAULT_DESC=$(wpctl inspect @DEFAULT_AUDIO_SINK@ | awk -F'"' '/node\.description/{print $2; exit}')

# Gain state (dB).
GLOW=0.0
GMID=0.0
GHI=0.0
BYPASS=0

# Streaming pink noise generator. Writes WAV header with max data size, then
# generates samples to stdout forever until SIGPIPE. pw-cat reads header once,
# plays continuously — no loop seam.
render_pink_gen() {
  cat > "$NOISE_GEN" <<'PY'
import math, struct, sys, random
SR = 48000; CH = 2; BPS = 16
BR = SR*CH*BPS//8; BA = CH*BPS//8
DS = 0x7FFFFF00  # ~2 GB; pw-cat keeps reading regardless
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
CHUNK = 2048  # frames per write -> 4096 samples -> ~43 ms latency
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
  if [[ -f "$EQ_CONF" ]] && grep -q "$SINK_NAME" "$EQ_CONF"; then
    echo "config already present at $EQ_CONF"
    return 0
  fi
  mkdir -p "$PW_CONF_DIR"
  cat > "$EQ_CONF" <<'EOF'
context.modules = [
  { name = libpipewire-module-filter-chain
    args = {
      node.description = "Banks EQ"
      media.name       = "Banks EQ"
      filter.graph = {
        nodes = [
          { type = builtin name = low_l label = bq_peaking
            control = { Freq =    80 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = mid_l label = bq_peaking
            control = { Freq =  1000 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = hi_l  label = bq_highshelf
            control = { Freq = 10000 Q = 0.7 Gain = 0.0 } }
          { type = builtin name = low_r label = bq_peaking
            control = { Freq =    80 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = mid_r label = bq_peaking
            control = { Freq =  1000 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = hi_r  label = bq_highshelf
            control = { Freq = 10000 Q = 0.7 Gain = 0.0 } }
        ]
        links = [
          { output = "low_l:Out" input = "mid_l:In" }
          { output = "mid_l:Out" input = "hi_l:In"  }
          { output = "low_r:Out" input = "mid_r:In" }
          { output = "mid_r:Out" input = "hi_r:In"  }
        ]
        inputs  = [ "low_l:In"  "low_r:In" ]
        outputs = [ "hi_l:Out"  "hi_r:Out" ]
      }
      capture.props  = {
        node.name      = "banks_eq"
        media.class    = "Audio/Sink"
        audio.channels = 2
        audio.position = [ FL FR ]
      }
      playback.props = {
        node.name      = "banks_eq_out"
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
  for i in 1 2 3 4 5 6 7 8 9 10; do
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

# Apply current gain state to filter-chain (or zero if bypassed).
apply_gains() {
  local gl gm gh
  if (( BYPASS )); then gl=0.0; gm=0.0; gh=0.0
  else gl=$GLOW; gm=$GMID; gh=$GHI; fi
  pw-cli s "$EQ_ID" Props \
    "{ params = [ \"low_l:Gain\" $gl  \"low_r:Gain\" $gl \
                  \"mid_l:Gain\" $gm  \"mid_r:Gain\" $gm \
                  \"hi_l:Gain\"  $gh  \"hi_r:Gain\"  $gh ] }" >/dev/null 2>&1
}

bar() {
  # render -12..+12 dB as 24-cell bar
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
== Banks live EQ ==
  source: pink noise loop  (sink: banks_eq -> $PREV_DEFAULT_DESC)
  bypass: $([[ $BYPASS -eq 1 ]] && echo "ON  (signal flat)" || echo "off (EQ active)")

  low  80 Hz   peaking      $(printf '%+5.1f dB ' "$GLOW")  [$(bar "$GLOW")]
  mid   1 kHz  peaking      $(printf '%+5.1f dB ' "$GMID")  [$(bar "$GMID")]
  hi   10 kHz  high-shelf   $(printf '%+5.1f dB ' "$GHI")   [$(bar "$GHI")]

  keys:
    low:  q/w  -1/+1 dB    a/s  -3/+3 dB    z  zero
    mid:  e/r  -1/+1 dB    d/f  -3/+3 dB    x  zero
    hi:   t/y  -1/+1 dB    g/h  -3/+3 dB    c  zero
    presets:  1 flat    2 bass+    3 voice    4 treble+    5 V-shape
    space: bypass toggle      0: zero all      Q/Ctrl-C: quit
EOF
}

clamp() {
  awk -v v="$1" 'BEGIN{ if(v>12)v=12; if(v<-12)v=-12; printf "%.1f", v }'
}

bump() {
  # $1 = var name, $2 = delta (signed dB)
  local cur new
  case "$1" in
    GLOW) cur=$GLOW;;
    GMID) cur=$GMID;;
    GHI)  cur=$GHI;;
  esac
  new=$(awk -v c="$cur" -v d="$2" 'BEGIN{printf "%.1f", c+d}')
  new=$(clamp "$new")
  case "$1" in
    GLOW) GLOW=$new;;
    GMID) GMID=$new;;
    GHI)  GHI=$new;;
  esac
}

preset() {
  case "$1" in
    flat)     GLOW=0.0;  GMID=0.0;  GHI=0.0  ;;
    bass)     GLOW=6.0;  GMID=0.0;  GHI=-1.0 ;;
    voice)    GLOW=-3.0; GMID=4.0;  GHI=2.0  ;;
    treble)   GLOW=-1.0; GMID=0.0;  GHI=6.0  ;;
    vshape)   GLOW=5.0;  GMID=-4.0; GHI=5.0  ;;
  esac
  BYPASS=0
}

# Background streaming player. setsid so we can kill the whole process group
# (python + pw-cat) cleanly.
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
    # Restore previous default by walking wpctl status for matching description.
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

apply_gains
start_noise

echo
echo "press any key to start interactive loop (or Ctrl-C to abort)..."
IFS= read -rsn1 _start
# Interactive loop. Short read timeout so Ctrl-C is responsive even on busybox.
while :; do
  ui
  if ! IFS= read -rsn1 -t 86400 key; then
    continue
  fi
  case "$key" in
    q) bump GLOW -1 ;;
    w) bump GLOW +1 ;;
    a) bump GLOW -3 ;;
    s) bump GLOW +3 ;;
    z) GLOW=0.0 ;;

    e) bump GMID -1 ;;
    r) bump GMID +1 ;;
    d) bump GMID -3 ;;
    f) bump GMID +3 ;;
    x) GMID=0.0 ;;

    t) bump GHI -1 ;;
    y) bump GHI +1 ;;
    g) bump GHI -3 ;;
    h) bump GHI +3 ;;
    c) GHI=0.0 ;;

    1) preset flat ;;
    2) preset bass ;;
    3) preset voice ;;
    4) preset treble ;;
    5) preset vshape ;;

    ' ') BYPASS=$((1 - BYPASS)) ;;
    0) GLOW=0.0; GMID=0.0; GHI=0.0; BYPASS=0 ;;
    Q) break ;;
  esac
  apply_gains
done
