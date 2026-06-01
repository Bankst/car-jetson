#!/usr/bin/env python3
"""banks-audio-hw — live HW PEQ + MVC tester on Tegra194 OPE1.

Replaces the userspace banks-eq-live.sh prototype with the real silicon
path: amixer cset to MVC1 (HW-ramped master volume) and to the OPE1 PEQ
biquad gain / shift RAM (12 stages x 8 channels, Q1.30 fixed-point).

Coefficients are designed in-process via RBJ Audio EQ Cookbook formulas
and quantised to int32 before being written to ALSA INTEGER-array
controls. PEQ stages count is set to N=8 (active) for parity with the
prior SW tester; the silicon supports up to 12.

Interactive keys:
  1..8     select band cursor
  - / +    bump active band gain by 1 dB
  [ / ]    bump active band gain by 0.25 dB
  \\       zero active band
  r        reset all bands to flat
  b        toggle PEQ Active (HW bypass)
  m        toggle MVC1 mute
  up/down  MVC1 volume +/- 100 (1 dB nominal)
  p        cycle presets (flat / bass+ / voice / treble+ / V-shape)
  n        start/stop internal pink-noise generator
  q / Q    quit (leaves last-applied state in place; route preserved)

Assumes the banks-audio-route boot service has already wired
ADMAIF1 -> MVC1 -> OPE1 -> I2S5. Run as root on the devkit.
"""

from __future__ import annotations

import math
import os
import select
import signal
import struct
import subprocess
import sys
import termios
import threading
import time
import tty

CARD = "APE"
SR = 48000
SHIFT = 30  # Q1.30 — handles |a1| < 2 safely
N_ACTIVE_BANDS = 8  # of 12 silicon stages
GAIN_STEP_BIG = 1.0
GAIN_STEP_SMALL = 0.25
MVC_STEP = 100  # MVC1 linear curve: 100 ~= 1 dB

# ---------------------------------------------------------------------------
# numid map — captured from devkit `amixer -c APE controls`
# ---------------------------------------------------------------------------
NUMID = {
    "peq_active":    1135,
    "peq_stages":    1136,
    "peq_gain_ch0":  1137,
    "peq_gain_ch1":  1138,
    "peq_shift_ch0": 1145,
    "peq_shift_ch1": 1146,
    "mvc1_volume":   1111,
    "mvc1_mute":     1112,
}

# ---------------------------------------------------------------------------
# Bands — 12 silicon slots, first N_ACTIVE_BANDS used; rest pinned identity.
# (type, freq_hz, q, label)
# ---------------------------------------------------------------------------
BANDS = [
    ("lowshelf",     60, 0.707, "60 Hz"),
    ("peaking",     150, 1.0,   "150 Hz"),
    ("peaking",     400, 1.0,   "400 Hz"),
    ("peaking",    1000, 1.0,   "1 kHz"),
    ("peaking",    2500, 1.0,   "2.5 kHz"),
    ("peaking",    6000, 1.0,   "6 kHz"),
    ("peaking",   10000, 1.0,   "10 kHz"),
    ("highshelf", 15000, 0.707, "15 kHz"),
]
assert len(BANDS) == N_ACTIVE_BANDS

PRESETS = {
    "flat":     [ 0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0],
    "bass+":    [ 6.0,  4.0,  2.0,  0.0,  0.0,  0.0,  0.0,  0.0],
    "voice":    [-3.0, -2.0,  1.0,  3.0,  4.0,  2.0,  0.0, -2.0],
    "treble+":  [ 0.0,  0.0,  0.0,  0.0,  1.0,  3.0,  5.0,  6.0],
    "v-shape":  [ 5.0,  3.0,  0.0, -3.0, -3.0,  0.0,  3.0,  5.0],
}
PRESET_ORDER = ["flat", "bass+", "voice", "treble+", "v-shape"]


# ---------------------------------------------------------------------------
# RBJ Audio EQ Cookbook biquad designers
# ---------------------------------------------------------------------------
def rbj_peaking(freq, q, gain_db, sr=SR):
    a = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * freq / sr
    cw = math.cos(w0)
    alpha = math.sin(w0) / (2 * q)
    b0 = 1 + alpha * a
    b1 = -2 * cw
    b2 = 1 - alpha * a
    a0 = 1 + alpha / a
    a1 = -2 * cw
    a2 = 1 - alpha / a
    return (b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)


def rbj_lowshelf(freq, q, gain_db, sr=SR):
    a = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * freq / sr
    cw = math.cos(w0)
    alpha = math.sin(w0) / 2 * math.sqrt((a + 1/a) * (1/q - 1) + 2)
    twoSqrtA = 2 * math.sqrt(a)
    b0 = a * ((a + 1) - (a - 1) * cw + twoSqrtA * alpha)
    b1 = 2 * a * ((a - 1) - (a + 1) * cw)
    b2 = a * ((a + 1) - (a - 1) * cw - twoSqrtA * alpha)
    a0 = (a + 1) + (a - 1) * cw + twoSqrtA * alpha
    a1 = -2 * ((a - 1) + (a + 1) * cw)
    a2 = (a + 1) + (a - 1) * cw - twoSqrtA * alpha
    return (b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)


def rbj_highshelf(freq, q, gain_db, sr=SR):
    a = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * freq / sr
    cw = math.cos(w0)
    alpha = math.sin(w0) / 2 * math.sqrt((a + 1/a) * (1/q - 1) + 2)
    twoSqrtA = 2 * math.sqrt(a)
    b0 = a * ((a + 1) + (a - 1) * cw + twoSqrtA * alpha)
    b1 = -2 * a * ((a - 1) + (a + 1) * cw)
    b2 = a * ((a + 1) + (a - 1) * cw - twoSqrtA * alpha)
    a0 = (a + 1) - (a - 1) * cw + twoSqrtA * alpha
    a1 = 2 * ((a - 1) - (a + 1) * cw)
    a2 = (a + 1) - (a - 1) * cw - twoSqrtA * alpha
    return (b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)


DESIGNERS = {
    "peaking":   rbj_peaking,
    "lowshelf":  rbj_lowshelf,
    "highshelf": rbj_highshelf,
}

INT32_MAX = 0x7FFFFFFF
INT32_MIN = -0x80000000


def quantize(x, shift=SHIFT):
    """Q(31-shift).shift signed int32."""
    scale = 1 << shift
    v = int(round(x * scale))
    if v > INT32_MAX:
        return INT32_MAX
    if v < INT32_MIN:
        return INT32_MIN
    return v


def build_gain_blob(gains_db):
    """Build the 62 x s32 PEQ gain blob for one channel.

    Layout per validator: [pre_gain] + [12 bands x 5 coeffs] + [post_gain].
    Unused bands (indices >= N_ACTIVE_BANDS) and zero-gain bands carry
    identity coefficients (b0=1, rest=0) so they pass signal through.
    """
    out = [quantize(1.0)]  # pre_gain unity
    for i in range(12):
        if i < N_ACTIVE_BANDS and abs(gains_db[i]) > 0.001:
            btype, freq, q, _ = BANDS[i]
            coeffs = DESIGNERS[btype](freq, q, gains_db[i])
        else:
            coeffs = (1.0, 0.0, 0.0, 0.0, 0.0)
        out.extend(quantize(c) for c in coeffs)
    out.append(quantize(1.0))  # post_gain unity
    assert len(out) == 62, f"gain blob length {len(out)} != 62"
    return out


def build_shift_blob():
    """14-int shift blob: pre + 12 bands + post, all = SHIFT."""
    return [SHIFT] * 14


# ---------------------------------------------------------------------------
# amixer drivers
# ---------------------------------------------------------------------------
def amixer_cset(numid, value):
    """value may be int, str, or iterable of ints (comma-joined for arrays)."""
    if isinstance(value, (list, tuple)):
        arg = ",".join(str(v) for v in value)
    else:
        arg = str(value)
    r = subprocess.run(
        ["amixer", "-c", CARD, "cset", f"numid={numid}", "--", arg],
        capture_output=True, text=True
    )
    return r.returncode == 0, r.stderr


def write_eq(gains_db):
    blob = build_gain_blob(gains_db)
    shift = build_shift_blob()
    ok0, _ = amixer_cset(NUMID["peq_gain_ch0"], blob)
    ok1, _ = amixer_cset(NUMID["peq_gain_ch1"], blob)
    amixer_cset(NUMID["peq_shift_ch0"], shift)
    amixer_cset(NUMID["peq_shift_ch1"], shift)
    return ok0 and ok1


def set_peq_active(on):
    amixer_cset(NUMID["peq_active"], "on" if on else "off")


def set_peq_stages_active(n_active):
    # Driver stores N-1 in PEQ_CONFIG_0[5:2]; ALSA control accepts 0..11
    amixer_cset(NUMID["peq_stages"], n_active - 1)


def set_mvc_volume(int_val):
    int_val = max(0, min(16000, int_val))
    amixer_cset(NUMID["mvc1_volume"], int_val)
    return int_val


def set_mvc_mute(on):
    amixer_cset(NUMID["mvc1_mute"], "on" if on else "off")


# ---------------------------------------------------------------------------
# Pink-noise generator — streaming WAV over pw-cat stdin (gapless)
# ---------------------------------------------------------------------------
def write_max_wav_header(sr=SR, channels=2, bps=16):
    # Voss-McCartney needs no header for raw, but pw-cat -p reads WAV only.
    # Trick: declare a near-2-GB data chunk so pw-cat keeps reading from
    # stdin indefinitely. See banks-eq-live.sh for context.
    data_size = 0x7FFFFF00
    byte_rate = sr * channels * bps // 8
    block_align = channels * bps // 8
    return (
        b"RIFF" + struct.pack("<I", 36 + data_size) + b"WAVE"
        + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, sr,
                                byte_rate, block_align, bps)
        + b"data" + struct.pack("<I", data_size)
    )


def pink_sample_generator(sr=SR):
    """Voss-McCartney 1/f pink-noise, 16-bit stereo samples."""
    import random
    r = random.Random(0xC4F3)
    n_rows = 16
    rows = [0.0] * n_rows
    running = 0.0
    counter = 0
    while True:
        # Pick which row to refresh by trailing-zero count
        n = (counter ^ (counter + 1))
        idx = 0
        while n:
            n >>= 1
            idx += 1
        idx = min(idx - 1, n_rows - 1) if idx > 0 else 0
        old = rows[idx]
        new = r.uniform(-1.0, 1.0)
        rows[idx] = new
        running += new - old
        counter = (counter + 1) & 0xFFFF
        sample = max(-1.0, min(1.0, running / n_rows * 0.25))
        s16 = int(sample * 30000)
        yield s16, s16


class PinkNoise:
    def __init__(self):
        self.proc = None
        self.thr = None
        self.stop_flag = threading.Event()

    def start(self):
        if self.proc:
            return
        self.stop_flag.clear()
        self.proc = subprocess.Popen(
            ["pw-cat", "-p", "-"],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid,
        )
        self.proc.stdin.write(write_max_wav_header())
        self.proc.stdin.flush()
        self.thr = threading.Thread(target=self._feed, daemon=True)
        self.thr.start()

    def _feed(self):
        gen = pink_sample_generator()
        buf = bytearray()
        try:
            while not self.stop_flag.is_set():
                buf.clear()
                for _ in range(1024):
                    l, r = next(gen)
                    buf += struct.pack("<hh", l, r)
                self.proc.stdin.write(bytes(buf))
                self.proc.stdin.flush()
        except (BrokenPipeError, ValueError, OSError):
            pass

    def stop(self):
        self.stop_flag.set()
        if self.proc:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            except (ProcessLookupError, PermissionError):
                pass
            try:
                self.proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
            self.proc = None


# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------
ANSI_CLEAR = "\033[2J\033[H"
ANSI_LINE_CLEAR = "\033[K"
ANSI_BOLD = "\033[1m"
ANSI_REV = "\033[7m"
ANSI_DIM = "\033[2m"
ANSI_RESET = "\033[0m"


def render(state):
    out = [ANSI_CLEAR]
    out.append(ANSI_BOLD + "banks-audio-hw — live OPE1 PEQ + MVC1\n" + ANSI_RESET)
    out.append(f"  PEQ Active: {ANSI_BOLD}{'ON ' if state['peq_active'] else 'OFF'}{ANSI_RESET}")
    out.append(f"  | MVC1: {state['mvc_volume']:5d}/16000 ({(state['mvc_volume']-12000)/100:+.1f} dB)")
    out.append(f"  | Mute: {'ON' if state['mvc_mute'] else 'off'}")
    out.append(f"  | Preset: {state['preset_name'] or '-'}")
    out.append(f"  | Noise: {'ON' if state['noise_on'] else 'off'}\n")
    out.append("\n  bands:\n")
    for i, (btype, freq, q, label) in enumerate(BANDS):
        sel = i == state["cursor"]
        cursor = ANSI_REV + ">" + ANSI_RESET if sel else " "
        g = state["gains"][i]
        bar_left = max(0, int(-g * 2))
        bar_right = max(0, int(g * 2))
        bar = " " * (16 - bar_left) + ("-" * bar_left) + "|" + ("+" * bar_right)
        bar = bar[:34]
        out.append(f"  {cursor} {i+1}. {btype:9s} {label:>8s}  Q={q:.2f}  {g:+6.2f} dB  [{bar}]")
    out.append("\n")
    out.append(ANSI_DIM + "  keys: 1-8 select | -/+ ±1dB | [/] ±0.25dB | \\ zero | r reset | b bypass\n")
    out.append("        m mute | up/down vol | p preset | n noise | q quit" + ANSI_RESET + "\n")
    sys.stdout.write("".join(out))
    sys.stdout.flush()


def read_key(timeout=1.0):
    """Return a single key (or a multi-char escape like 'UP'/'DOWN'), or ''."""
    r, _, _ = select.select([sys.stdin], [], [], timeout)
    if not r:
        return ""
    ch = sys.stdin.read(1)
    if ch != "\x1b":
        return ch
    # Could be ESC alone or start of CSI sequence — try non-blocking read
    r2, _, _ = select.select([sys.stdin], [], [], 0.05)
    if not r2:
        return "ESC"
    seq = sys.stdin.read(2)
    if seq == "[A":
        return "UP"
    if seq == "[B":
        return "DOWN"
    if seq == "[C":
        return "RIGHT"
    if seq == "[D":
        return "LEFT"
    return "ESC"


def run_ui():
    state = {
        "cursor":      3,  # default at 1 kHz band
        "gains":       [0.0] * N_ACTIVE_BANDS,
        "peq_active":  False,
        "mvc_mute":    False,
        "mvc_volume":  11500,
        "preset_name": "flat",
        "noise_on":    False,
        "preset_idx":  0,
    }
    pink = PinkNoise()

    # Establish HW state: stages = 8, write flat EQ, leave active=off
    set_peq_stages_active(N_ACTIVE_BANDS)
    write_eq(state["gains"])
    set_mvc_volume(state["mvc_volume"])

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setcbreak(fd)
        render(state)
        while True:
            k = read_key(0.5)
            if not k:
                continue

            dirty = False

            if k in ("q", "Q"):
                break
            elif k.isdigit() and "1" <= k <= str(N_ACTIVE_BANDS):
                state["cursor"] = int(k) - 1
                dirty = True
            elif k == "-":
                state["gains"][state["cursor"]] -= GAIN_STEP_BIG
                write_eq(state["gains"])
                dirty = True
            elif k == "+" or k == "=":
                state["gains"][state["cursor"]] += GAIN_STEP_BIG
                write_eq(state["gains"])
                dirty = True
            elif k == "[":
                state["gains"][state["cursor"]] -= GAIN_STEP_SMALL
                write_eq(state["gains"])
                dirty = True
            elif k == "]":
                state["gains"][state["cursor"]] += GAIN_STEP_SMALL
                write_eq(state["gains"])
                dirty = True
            elif k == "\\":
                state["gains"][state["cursor"]] = 0.0
                write_eq(state["gains"])
                dirty = True
            elif k == "r":
                state["gains"] = [0.0] * N_ACTIVE_BANDS
                state["preset_name"] = "flat"
                state["preset_idx"] = 0
                write_eq(state["gains"])
                dirty = True
            elif k == "b":
                state["peq_active"] = not state["peq_active"]
                set_peq_active(state["peq_active"])
                dirty = True
            elif k == "m":
                state["mvc_mute"] = not state["mvc_mute"]
                set_mvc_mute(state["mvc_mute"])
                dirty = True
            elif k == "UP":
                state["mvc_volume"] = set_mvc_volume(state["mvc_volume"] + MVC_STEP)
                dirty = True
            elif k == "DOWN":
                state["mvc_volume"] = set_mvc_volume(state["mvc_volume"] - MVC_STEP)
                dirty = True
            elif k == "p":
                state["preset_idx"] = (state["preset_idx"] + 1) % len(PRESET_ORDER)
                name = PRESET_ORDER[state["preset_idx"]]
                state["preset_name"] = name
                state["gains"] = list(PRESETS[name])
                write_eq(state["gains"])
                # Auto-activate when preset selected; flat is harmless
                if not state["peq_active"]:
                    state["peq_active"] = True
                    set_peq_active(True)
                dirty = True
            elif k == "n":
                if state["noise_on"]:
                    pink.stop()
                    state["noise_on"] = False
                else:
                    pink.start()
                    state["noise_on"] = True
                dirty = True

            if dirty:
                # Clamp gains
                for i in range(N_ACTIVE_BANDS):
                    state["gains"][i] = max(-24.0, min(24.0, state["gains"][i]))
                render(state)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        pink.stop()
        sys.stdout.write("\n")
        sys.stdout.flush()


if __name__ == "__main__":
    if os.geteuid() != 0:
        print("banks-audio-hw: must run as root (amixer write to APE card)", file=sys.stderr)
        sys.exit(1)
    try:
        run_ui()
    except KeyboardInterrupt:
        pass
