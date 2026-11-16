#!/usr/bin/env python3
"""
Build 8ch S32_LE raw audio test files from /data/vox/ word clips.

Usage:
  python3 /data/vox_build.py sequential
  python3 /data/vox_build.py all "attention all systems online"
  python3 /data/vox_build.py custom 0 "front left test" 3 "sub test"

Play:
  aplay -D hw:1,0 -f S32_LE -r 48000 -c 8 -t raw /data/vox_out.raw
"""
import struct, os, math, sys

RATE = 48000
CH = 8
VOX = "/data/vox"
OUT = "/data/vox_out.raw"

SENTENCES = [
    "front left test",
    "front right test",
    "center test",
    "sub test",
    "side left test",
    "side right test",
    "back left test",
    "back right test",
]

TONES = ["doop", "deeoo", "dadeda", "buzwarn", "bloop", "bizwarn", "woop", "doop"]

def load_wav(name):
    path = os.path.join(VOX, name + ".wav")
    if not os.path.exists(path):
        print(f"  WARN: {name}.wav not found", file=sys.stderr)
        return []
    with open(path, "rb") as f:
        data = f.read()
    idx = data.find(b"data")
    if idx < 0: return []
    sz = struct.unpack_from("<I", data, idx+4)[0]
    raw = data[idx+8:idx+8+sz]
    sr = struct.unpack_from("<I", data, 24)[0]
    bps = struct.unpack_from("<H", data, 34)[0]
    if bps == 8:
        samples = [((b - 128) << 7) for b in raw]
    elif bps == 16:
        n = len(raw)//2
        samples = list(struct.unpack(f"<{n}h", raw[:n*2]))
    else:
        return []
    out = []
    for i in range(int(len(samples) * RATE / sr)):
        si = min(int(i * sr / RATE), len(samples)-1)
        out.append(samples[si])
    return out

def make_tone(ch_idx):
    return load_wav(TONES[ch_idx % len(TONES)])

def words_to_samples(sentence):
    s = []
    for w in sentence.split():
        s.extend(load_wav(w))
        s.extend([0] * int(RATE * 0.1))
    return s

def build_sequential(slot_dur=4.0):
    sd = int(RATE * slot_dur)
    total = sd * CH
    buf = bytearray(total * CH * 4)
    for ch in range(CH):
        s = words_to_samples(SENTENCES[ch])
        s.extend([0] * int(RATE * 0.15))
        s.extend(make_tone(ch))
        if len(s) < sd:
            s.extend([0] * (sd - len(s)))
        else:
            s = s[:sd]
        base = ch * sd
        for i in range(sd):
            off = (base + i) * CH * 4 + ch * 4
            struct.pack_into("<i", buf, off, s[i] << 16)
    return buf, total

def build_all(sentence_str):
    mono = words_to_samples(sentence_str)
    mono.extend([0] * int(RATE * 0.2))
    pass  # no auto tone
    total = len(mono)
    buf = bytearray(total * CH * 4)
    o = 0
    for i in range(total):
        v = mono[i] << 16
        for c in range(CH):
            struct.pack_into("<i", buf, o, v)
            o += 4
    return buf, total

def build_custom(args):
    ch_audio = [[] for _ in range(CH)]
    i = 0
    while i < len(args):
        ch = int(args[i])
        sentence = args[i+1]
        ch_audio[ch] = words_to_samples(sentence)
        ch_audio[ch].extend([0] * int(RATE * 0.15))
        ch_audio[ch].extend(make_tone(ch))
        i += 2
    mx = max((len(s) for s in ch_audio), default=RATE)
    mx += int(RATE * 0.5)
    for ch in range(CH):
        ch_audio[ch].extend([0] * (mx - len(ch_audio[ch])))
    buf = bytearray(mx * CH * 4)
    o = 0
    for i in range(mx):
        for c in range(CH):
            struct.pack_into("<i", buf, o, ch_audio[c][i] << 16)
            o += 4
    return buf, mx

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "sequential"
    if mode == "sequential":
        buf, total = build_sequential()
    elif mode == "all":
        text = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "attention all systems online test complete"
        buf, total = build_all(text)
    elif mode == "custom":
        buf, total = build_custom(sys.argv[2:])
    else:
        print(f"Unknown mode: {mode}")
        sys.exit(1)
    with open(OUT, "wb") as f:
        f.write(buf)
    print(f"{OUT}: {len(buf)} bytes, {total/RATE:.1f}s")
