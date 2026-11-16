#!/bin/bash
# Banks audio CLI test harness — volume + filter-chain EQ.
# Target: jetson devkit, system-instance PipeWire as root.
set -uo pipefail

PW_CONF_DIR=/etc/pipewire/pipewire.conf.d
EQ_CONF="$PW_CONF_DIR/99-banks-eq.conf"
SINK_NAME="banks_eq"
TEST_FREQ=440
WAV_SINE=/tmp/banks-sine.wav
WAV_SWEEP=/tmp/banks-sweep.wav

c_hdr()  { printf "\n\033[1;36m== %s ==\033[0m\n" "$*"; }
c_ok()   { printf "  \033[32m✓\033[0m %s\n" "$*"; }
c_warn() { printf "  \033[33m!\033[0m %s\n" "$*"; }
c_err()  { printf "  \033[31m✗\033[0m %s\n" "$*"; }

require() { command -v "$1" >/dev/null || { c_err "missing: $1"; exit 1; }; }
for t in wpctl pw-cli pw-cat python3 systemctl; do require "$t"; done

PW_RESTART="systemctl restart pipewire wireplumber"

DEF_SINK_ID=""
DEF_SINK_NAME=""
EQ_ID=""

# Build WAV files up front. Inline RIFF header — yocto python3 stdlib lacks `wave`.
_PY_WAV_HEADER='
def wav_header(n_samples, sr=48000, channels=2, bps=16):
    import struct
    byte_rate = sr * channels * bps // 8
    block_align = channels * bps // 8
    data_size = n_samples * block_align
    return (b"RIFF" + struct.pack("<I", 36 + data_size) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, sr, byte_rate, block_align, bps)
            + b"data" + struct.pack("<I", data_size))
'

make_wav_sine() {
  python3 - "$1" "$2" "$3" <<PY
import math, struct, sys
${_PY_WAV_HEADER}
freq = float(sys.argv[1]); dur = float(sys.argv[2]); path = sys.argv[3]
sr = 48000; N = int(sr * dur)
with open(path, "wb") as f:
    f.write(wav_header(N, sr, 2, 16))
    for n in range(N):
        s = int(0.3 * 32767 * math.sin(2 * math.pi * freq * n / sr))
        f.write(struct.pack('<hh', s, s))
PY
}

make_wav_sweep() {
  python3 - "$1" "$2" <<PY
import math, struct, sys
${_PY_WAV_HEADER}
dur = float(sys.argv[1]); path = sys.argv[2]
sr = 48000; f0, f1 = 80.0, 10000.0; N = int(sr * dur); phase = 0.0
with open(path, "wb") as f:
    f.write(wav_header(N, sr, 2, 16))
    for n in range(N):
        t = n / sr
        ff = f0 * (f1 / f0) ** (t / dur)
        phase += 2 * math.pi * ff / sr
        s = int(0.25 * 32767 * math.sin(phase))
        f.write(struct.pack('<hh', s, s))
PY
}

#--- 1. discovery -------------------------------------------------------------
phase_discover() {
  c_hdr "discovery"
  wpctl status | awk '/Audio/,/Video/' | head -40
  DEF_SINK_ID=$(wpctl inspect @DEFAULT_AUDIO_SINK@ | awk '/^id /{gsub(",","",$2); print $2; exit}')
  DEF_SINK_NAME=$(wpctl inspect @DEFAULT_AUDIO_SINK@ | awk -F'"' '/node\.name/{print $2; exit}')
  [[ -z "$DEF_SINK_ID" ]] && { c_err "no default sink"; exit 1; }
  c_ok "default sink: id=$DEF_SINK_ID name=$DEF_SINK_NAME"
}

#--- 2. volume ----------------------------------------------------------------
phase_volume() {
  c_hdr "volume ramp"
  c_ok "rendering test tone -> $WAV_SINE"
  make_wav_sine "$TEST_FREQ" 4 "$WAV_SINE"

  local orig
  orig=$(wpctl get-volume @DEFAULT_AUDIO_SINK@ | awk '{print $2}')
  c_ok "saved: $orig"

  pw-cat -p "$WAV_SINE" >/dev/null 2>&1 &
  local pid=$!

  for v in 0.20 0.40 0.60 0.40 0.20; do
    wpctl set-volume @DEFAULT_AUDIO_SINK@ "$v"
    c_ok "vol=$v"
    sleep 0.6
  done

  wpctl set-mute @DEFAULT_AUDIO_SINK@ 1; c_ok "muted"; sleep 0.5
  wpctl set-mute @DEFAULT_AUDIO_SINK@ 0; c_ok "unmuted"; sleep 0.5

  wait "$pid" 2>/dev/null || true
  wpctl set-volume @DEFAULT_AUDIO_SINK@ "$orig"
  c_ok "restored: $orig"
}

#--- 3. filter-chain install --------------------------------------------------
# Parallel L/R chains. 3 bands per channel: low(80) mid(1k) high-shelf(10k).
phase_eq_install() {
  c_hdr "filter-chain install"
  mkdir -p "$PW_CONF_DIR"

  cat > "$EQ_CONF" <<'EOF'
context.modules = [
  { name = libpipewire-module-filter-chain
    args = {
      node.description = "Banks EQ"
      media.name       = "Banks EQ"
      filter.graph = {
        nodes = [
          { type = builtin name = low_l   label = bq_peaking
            control = { Freq =    80 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = mid_l   label = bq_peaking
            control = { Freq =  1000 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = hi_l    label = bq_highshelf
            control = { Freq = 10000 Q = 0.7 Gain = 0.0 } }

          { type = builtin name = low_r   label = bq_peaking
            control = { Freq =    80 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = mid_r   label = bq_peaking
            control = { Freq =  1000 Q = 1.0 Gain = 0.0 } }
          { type = builtin name = hi_r    label = bq_highshelf
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
  c_ok "wrote $EQ_CONF"

  $PW_RESTART
  for i in 1 2 3 4 5 6 7 8 9 10; do
    sleep 1
    if systemctl is-active --quiet pipewire && wpctl status >/dev/null 2>&1; then
      break
    fi
  done

  if ! systemctl is-active --quiet pipewire; then
    c_err "pipewire failed to come up; check journalctl -u pipewire"
    journalctl -u pipewire -n 20 --no-pager | tail -12
    return 1
  fi

  EQ_ID=$(wpctl status | awk -v s="$SINK_NAME" '
    $0 ~ s {
      for(i=1;i<=NF;i++) if(match($i,/^[0-9]+\.$/)){gsub(/\./,"",$i); print $i; exit}
    }')
  if [[ -z "$EQ_ID" ]]; then
    c_err "filter-chain node not present"
    wpctl status | sed -n '/Audio/,/Video/p'
    return 1
  fi
  c_ok "EQ node id=$EQ_ID"

  wpctl set-default "$EQ_ID"
  c_ok "default sink → EQ"
}

#--- 4. live tweak ------------------------------------------------------------
phase_eq_tweak() {
  c_hdr "live param sweep"
  c_warn "sweep with +6dB lift per band, then flat. listen for boost"

  make_wav_sweep 10 "$WAV_SWEEP"
  pw-cat -p "$WAV_SWEEP" >/dev/null 2>&1 &
  local pid=$!

  # Try param set on each band pair (L+R together).
  for stage in low mid hi; do
    if pw-cli s "$EQ_ID" Props "{ params = [ \"${stage}_l:Gain\" 6.0 \"${stage}_r:Gain\" 6.0 ] }" >/dev/null 2>&1; then
      c_ok "+6dB ${stage}"
    else
      c_warn "set ${stage} Gain via pw-cli failed; live param schema may differ"
    fi
    sleep 1.5
    pw-cli s "$EQ_ID" Props "{ params = [ \"${stage}_l:Gain\" 0.0 \"${stage}_r:Gain\" 0.0 ] }" >/dev/null 2>&1
    sleep 0.3
  done

  wait "$pid" 2>/dev/null || true
}

#--- 5. teardown --------------------------------------------------------------
phase_teardown() {
  c_hdr "teardown"
  rm -f "$EQ_CONF" "$WAV_SINE" "$WAV_SWEEP" && c_ok "removed config + temp wavs"
  $PW_RESTART
  sleep 2
  if [[ -n "${DEF_SINK_NAME:-}" ]]; then
    local new_id
    new_id=$(wpctl status | awk -v n="$DEF_SINK_NAME" '
      BEGIN{want=0}
      /Audio/{want=1} /Video/{want=0}
      want && index($0, n){
        for(i=1;i<=NF;i++) if(match($i,/^[0-9]+\.$/)){gsub(/\./,"",$i); print $i; exit}
      }')
    if [[ -n "$new_id" ]]; then
      wpctl set-default "$new_id" && c_ok "default → $DEF_SINK_NAME (id=$new_id)"
    else
      c_warn "could not relocate $DEF_SINK_NAME; pick manually with wpctl set-default <id>"
    fi
  fi
}

trap phase_teardown EXIT INT TERM

phase_discover
phase_volume
phase_eq_install || { c_err "EQ install failed; aborting tweak phase"; exit 1; }
phase_eq_tweak

c_hdr "done"
