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
import threading
import time
# termios + tty are imported lazily inside run_ui() — the yocto python3
# stdlib subset on our image does not always ship them.

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
# libasound (ALSA control API) via ctypes.
#
# amixer's `cset` for INTEGER arrays writes only the first element — confirmed
# experimentally against numid=1137 (PEQ Channel-N biquad gain params, 62-int
# array) where every form of amixer invocation left RAM unchanged. The kernel
# driver's put handler in tegra210_peq.c:tegra210_peq_ahub_ram_put expects all
# N integers in ucontrol->value.integer.value[], so we need a proper
# snd_ctl_elem_write() call. ctypes against libasound.so.2 gives us that with
# no extra deps on the yocto image.
# ---------------------------------------------------------------------------
import ctypes

_asound = ctypes.CDLL("libasound.so.2")

_asound.snd_ctl_open.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_char_p, ctypes.c_int]
_asound.snd_ctl_open.restype = ctypes.c_int
_asound.snd_ctl_close.argtypes = [ctypes.c_void_p]
_asound.snd_ctl_close.restype = ctypes.c_int
_asound.snd_ctl_elem_id_malloc.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
_asound.snd_ctl_elem_id_malloc.restype = ctypes.c_int
_asound.snd_ctl_elem_id_free.argtypes = [ctypes.c_void_p]
_asound.snd_ctl_elem_id_set_numid.argtypes = [ctypes.c_void_p, ctypes.c_uint]
_asound.snd_ctl_elem_id_set_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_asound.snd_ctl_elem_id_set_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
_asound.snd_ctl_elem_value_malloc.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
_asound.snd_ctl_elem_value_malloc.restype = ctypes.c_int
_asound.snd_ctl_elem_value_free.argtypes = [ctypes.c_void_p]
_asound.snd_ctl_elem_value_set_id.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
_asound.snd_ctl_elem_value_set_integer.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_long]
_asound.snd_ctl_elem_value_set_boolean.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_long]
_asound.snd_ctl_elem_value_set_enumerated.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
_asound.snd_ctl_elem_write.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
_asound.snd_ctl_elem_write.restype = ctypes.c_int


def _open_ctl(card_name=f"hw:{CARD}"):
    ctl = ctypes.c_void_p()
    rc = _asound.snd_ctl_open(ctypes.byref(ctl), card_name.encode(), 0)
    if rc < 0:
        raise OSError(f"snd_ctl_open({card_name}) -> {rc}")
    return ctl


def _build_elem_value(numid):
    eid = ctypes.c_void_p()
    if _asound.snd_ctl_elem_id_malloc(ctypes.byref(eid)) < 0:
        raise MemoryError("snd_ctl_elem_id_malloc")
    _asound.snd_ctl_elem_id_set_numid(eid, numid)
    ev = ctypes.c_void_p()
    if _asound.snd_ctl_elem_value_malloc(ctypes.byref(ev)) < 0:
        _asound.snd_ctl_elem_id_free(eid)
        raise MemoryError("snd_ctl_elem_value_malloc")
    _asound.snd_ctl_elem_value_set_id(ev, eid)
    return eid, ev


def write_int_array(numid, values):
    ctl = _open_ctl()
    try:
        eid, ev = _build_elem_value(numid)
        try:
            for i, v in enumerate(values):
                _asound.snd_ctl_elem_value_set_integer(ev, i, int(v))
            rc = _asound.snd_ctl_elem_write(ctl, ev)
            return rc >= 0, rc
        finally:
            _asound.snd_ctl_elem_value_free(ev)
            _asound.snd_ctl_elem_id_free(eid)
    finally:
        _asound.snd_ctl_close(ctl)


def write_int_scalar(numid, value):
    return write_int_array(numid, [int(value)])


def write_bool(numid, on):
    ctl = _open_ctl()
    try:
        eid, ev = _build_elem_value(numid)
        try:
            _asound.snd_ctl_elem_value_set_boolean(ev, 0, 1 if on else 0)
            rc = _asound.snd_ctl_elem_write(ctl, ev)
            return rc >= 0, rc
        finally:
            _asound.snd_ctl_elem_value_free(ev)
            _asound.snd_ctl_elem_id_free(eid)
    finally:
        _asound.snd_ctl_close(ctl)


# ---------------------------------------------------------------------------
# Higher-level driver helpers
# ---------------------------------------------------------------------------
def write_eq(gains_db, currently_active=False):
    """Write 8-band stereo PEQ coefficients to OPE1.

    The PEQ RAM is write-locked while PEQ Active = on. Caller must
    deactivate first (or pass currently_active=True and we'll do the
    deactivate/restore dance). The keepalive stream must already be open
    so OPE1 is DAPM-active; otherwise regmap writes silently no-op.
    """
    blob = build_gain_blob(gains_db)
    shift = build_shift_blob()
    if currently_active:
        write_bool(NUMID["peq_active"], False)
        time.sleep(0.01)  # let the DSP settle / RAM unlock
    ok0, _ = write_int_array(NUMID["peq_gain_ch0"], blob)
    ok1, _ = write_int_array(NUMID["peq_gain_ch1"], blob)
    write_int_array(NUMID["peq_shift_ch0"], shift)
    write_int_array(NUMID["peq_shift_ch1"], shift)
    if currently_active:
        time.sleep(0.01)
        write_bool(NUMID["peq_active"], True)
    return ok0 and ok1


def set_peq_active(on):
    write_bool(NUMID["peq_active"], on)


def set_peq_stages_active(n_active):
    # Driver stores N-1 in PEQ_CONFIG_0[5:2]; ALSA control accepts 0..11.
    write_int_scalar(NUMID["peq_stages"], n_active - 1)


def set_mvc_volume(int_val):
    int_val = max(0, min(16000, int_val))
    write_int_scalar(NUMID["mvc1_volume"], int_val)
    return int_val


def set_mvc_mute(on):
    write_bool(NUMID["mvc1_mute"], on)


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


class _PwCatStream:
    """Spawn pw-cat with a streaming-WAV stdin pipe; feed from generator.
    Goes through PipeWire to whichever sink is default — useful for audible
    test signal (pink noise) but does NOT keep our AHUB OPE path energized
    if PipeWire's default sink lives outside the AHUB graph (e.g. HDA)."""

    def __init__(self, gen_factory):
        self.gen_factory = gen_factory
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
        gen = self.gen_factory()
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


class Keepalive:
    """Stream silence directly to hw:APE,0 (ADMAIF1) via aplay.

    Tegra OPE pm_runtime suspends when no DAPM-active stream traverses it,
    and PEQ RAM regmap writes to a suspended block silently no-op (the
    kernel handler returns 0 but the writes don't reach silicon). Holding
    a zero-filled stream open on ADMAIF1 keeps the ADMAIF1 -> MVC1 -> OPE1
    -> I2S5 graph DAPM-active so live EQ writes actually land.

    Routes around PipeWire entirely — pw-cat would target the default sink
    which may be HDA (HDMI audio) and leave the AHUB graph suspended.
    """

    def __init__(self):
        self.proc = None

    def start(self):
        if self.proc:
            return
        # `aplay -D hw:APE,0` opens pcm0p (ADMAIF1) directly. /dev/zero
        # gives us silent S16 stereo PCM at any sample rate we ask for.
        self.proc = subprocess.Popen(
            ["aplay", "-q", "-D", "hw:APE,0",
             "-f", "S16_LE", "-c", "2", "-r", str(SR),
             "/dev/zero"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setsid,
        )

    def stop(self):
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


class PinkNoise(_PwCatStream):
    def __init__(self):
        super().__init__(pink_sample_generator)


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


def _set_cbreak(fd):
    """Inline tty.setcbreak — the yocto python3 stdlib subset on our image
    ships termios but not the `tty` module wrappers."""
    import termios
    mode = termios.tcgetattr(fd)
    mode[3] = mode[3] & ~(termios.ECHO | termios.ICANON)  # LFLAG
    mode[6][termios.VMIN] = 1
    mode[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSAFLUSH, mode)


def run_ui():
    import termios
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

    # Spawn an inaudible silent keepalive stream BEFORE first EQ write.
    # Tegra OPE pm_runtime suspends when no DAPM-active stream flows through
    # it; PEQ RAM writes to a suspended block silently no-op (kernel handler
    # returns 0 but regmap writes don't reach silicon). Keeping a zero-filled
    # stream alive on ADMAIF1 keeps OPE energized so live EQ tweaks actually
    # land. Verified empirically against 62-int sentinel-pattern writes.
    keep = Keepalive()
    keep.start()
    time.sleep(0.25)  # let pcm0p reach RUNNING before first write

    # Establish HW state: stages = 8, write flat EQ, leave active=off
    set_peq_stages_active(N_ACTIVE_BANDS)
    write_eq(state["gains"], state["peq_active"])
    set_mvc_volume(state["mvc_volume"])

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        _set_cbreak(fd)
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
                write_eq(state["gains"], state["peq_active"])
                dirty = True
            elif k == "+" or k == "=":
                state["gains"][state["cursor"]] += GAIN_STEP_BIG
                write_eq(state["gains"], state["peq_active"])
                dirty = True
            elif k == "[":
                state["gains"][state["cursor"]] -= GAIN_STEP_SMALL
                write_eq(state["gains"], state["peq_active"])
                dirty = True
            elif k == "]":
                state["gains"][state["cursor"]] += GAIN_STEP_SMALL
                write_eq(state["gains"], state["peq_active"])
                dirty = True
            elif k == "\\":
                state["gains"][state["cursor"]] = 0.0
                write_eq(state["gains"], state["peq_active"])
                dirty = True
            elif k == "r":
                state["gains"] = [0.0] * N_ACTIVE_BANDS
                state["preset_name"] = "flat"
                state["preset_idx"] = 0
                write_eq(state["gains"], state["peq_active"])
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
                write_eq(state["gains"], state["peq_active"])
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
        keep.stop()
        sys.stdout.write("\n")
        sys.stdout.flush()


if __name__ == "__main__":
    if os.geteuid() != 0:
        print("banks-audio-hw: must run as root (amixer write to APE card)", file=sys.stderr)
        sys.exit(1)
    if not sys.stdin.isatty():
        print("banks-audio-hw: stdin is not a terminal — UI requires a real tty.", file=sys.stderr)
        print("                run from an interactive ssh session (no pipes / heredocs).", file=sys.stderr)
        sys.exit(1)
    try:
        run_ui()
    except KeyboardInterrupt:
        pass
