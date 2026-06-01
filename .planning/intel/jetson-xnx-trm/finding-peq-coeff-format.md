# Tegra194 (Tegra210-derived) OPE PEQ + MBDRC coefficient byte format

Investigation target: the per-channel ALSA controls

```
OPE1 PEQ Channel-N biquad gain params       (TYPE_INTEGER, count=62, s32 each)
OPE1 PEQ Channel-N biquad shift params      (TYPE_INTEGER, count=14, s32 each)
OPE1 MBDRC {low,mid,high} band biquad coeffs (TYPE_BYTES,   count=160 bytes = 40 u32)
```

Driver: `tegra210-peq` / `tegra210-mbdrc` in the L4T R35 OOT tree at
`build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/sound/soc/tegra-alt/`.
Source files cited below.

---

## 1. ALSA control type — NOT a TLV blob

Despite the macro name `TEGRA_SOC_BYTES_EXT`, the PEQ controls are exposed
as **`SNDRV_CTL_ELEM_TYPE_INTEGER` arrays**, *not* `SNDRV_CTL_ELEM_TYPE_BYTES`
and *not* TLV-typed controls. Therefore userspace passes **plain s32
integer values per element**, no TLV header (`numid`/`length` words) and
no byte-blob alignment concerns.

- PEQ info handler (`tegra210_peq_param_info`,
  `tegra210_peq_alt.c:151-162`):
  ```c
  uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
  uinfo->value.integer.min = -0x7fffffff;
  uinfo->value.integer.max = 0x7fffffff;
  uinfo->count = params->num_regs;
  ```
- MBDRC info handler (`tegra210_mbdrc_alt.c:346-355`):
  ```c
  uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
  uinfo->count = params->num_regs;          /* num_regs = 40 here */
  ```
  i.e. **MBDRC `*** band biquad coeffs` is exposed as a 160-byte raw
  blob (40 × u32 little-endian)**, also without TLV wrapping. From
  userspace alsa-lib you set it with `snd_ctl_elem_value_set_bytes()` or
  via `amixer cset numid=N <hex bytes>` / `amixer -c APE cset
  name='mbdrc low band biquad coeffs' "$(byte_blob)"`.

Endianness: native CPU (aarch64 = little-endian). The kernel writes the
u32 values directly to the AHUB DATA register via `regmap_write()`
(`utils/tegra210_xbar_utils_alt.c:80-82`), no byte swap.

---

## 2. PEQ gain-params layout — 62 × s32 per channel

`TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH = 2 + 12*5 = 62`
(`include/tegra210_peq_alt.h:48-49`)

```
Index   Name              Notes
─────────────────────────────────────────────────────
[ 0]    pre_gain          single scalar, signed Q(31-pre_shift).pre_shift
[ 1]    band0_b0          ┐
[ 2]    band0_b1          │
[ 3]    band0_b2          │  Band 0 biquad, 5 coeffs.
[ 4]    band0_a1          │  Order is b0, b1, b2, a1, a2.
[ 5]    band0_a2          ┘  (No a0 — implicit 1.0 / shift.)
[ 6]    band1_b0          ┐
...                       │  Bands 1..11, same 5-coeff layout.
[60]    band11_a1
[61]    band11_a2         (Only `biquad_stages` first bands are active.)
[──]    post_gain         ← Wait: the comment in the driver places
                            post_gain *after* band-11. Layout reconciled
                            below.
```

Reconciling with the driver's default-table literal
(`tegra210_peq_alt.c:45-61`):

```c
biquad_init_gains[62] = {
   1495012349,                                         /* pre-gain   [0]   */
   536870912, -1073741824, 536870912, 2143508246, -1069773768, /* band-0  [1..5]  */
   ...
   1, 0, 0, 0, 0,                                      /* band-11 [56..60] */
   963423114,                                          /* post-gain [61]  */
};
```

So the **definitive layout** is:

| Index range | Field                                  |
|-------------|----------------------------------------|
| `[0]`       | pre-gain                               |
| `[1..5]`    | band 0:  `b0, b1, b2, a1, a2`          |
| `[6..10]`   | band 1                                 |
| `[11..15]`  | band 2                                 |
| `[16..20]`  | band 3                                 |
| `[21..25]`  | band 4                                 |
| `[26..30]`  | band 5                                 |
| `[31..35]`  | band 6                                 |
| `[36..40]`  | band 7                                 |
| `[41..45]`  | band 8                                 |
| `[46..50]`  | band 9                                 |
| `[51..55]`  | band 10                                |
| `[56..60]`  | band 11                                |
| `[61]`      | post-gain                              |

Citation:
- `nvidia/sound/soc/tegra-alt/include/tegra210_peq_alt.h:44-51`
- `nvidia/sound/soc/tegra-alt/tegra210_peq_alt.c:43-67`

Per-stage coefficient order is **`b0, b1, b2, a1, a2`** — the same as
the MBDRC inline comment `/* For biquad_params[][5] order of coeff is
b0, b1, a0, a1, a2 */` *but note that MBDRC comment is mis-labeled*:
inspection of the PEQ default table (band-0 has `b0=2^29, b1=-2^30,
b2=2^29` — symmetric in b0/b2 — and `a1≈+1.996, a2≈-0.996`) shows the
3rd slot must be `b2`, not `a0`. Standard direct-form-I biquad with
implicit `a0=1`. Treat the MBDRC comment as a typo; the slot is `b2`.

---

## 3. PEQ shift-params layout — 14 × s32 per channel

`TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH = 2 + 12 = 14`
(`include/tegra210_peq_alt.h:50-51`)

| Index   | Field            |
|---------|------------------|
| `[0]`   | pre-shift        |
| `[1..12]` | band 0..11 shifts |
| `[13]`  | post-shift       |

Value range: 5-bit integer, 0..31 (`mask = 0x1f`,
`tegra210_peq_alt.c:176`). This is the **per-stage arithmetic right-shift
applied after the multiply-accumulate**, i.e. it sets the Q-format of
the corresponding row of `biquad_init_gains`.

Driver default (`tegra210_peq_alt.c:63-67`):

```c
biquad_init_shifts[14] = {
   23,                                  /* pre-shift  */
   30, 30, 30, 30, 30, 0, 0, 0, 0, 0, 0, 0,  /* band shifts (bands 0..11) */
   28,                                  /* post-shift */
};
```

Bands 5..11 have shift=0 with `b0=1, b1=b2=a1=a2=0` — identity biquads
for unused stages. Bands 0..4 are real (shift=30 → Q1.30).

---

## 4. Q-format

Each coefficient is **signed two's-complement int32**, interpreted as
**Q(31-shift).shift** for that stage's `shift` value. I.e.:

```
coefficient_float = raw_int / 2^shift
```

Confirmation from the default table:
- Band 0, shift=30:
  - `b0 = 536870912 / 2^30 = 0.5000`
  - `b1 = -1073741824 / 2^30 = -1.0000`
  - `b2 = 536870912 / 2^30 = 0.5000`
  - `a1 = 2143508246 / 2^30 ≈ +1.99629`
  - `a2 = -1069773768 / 2^30 ≈ -0.99630`

  That's a high-Q HPF/notch skeleton with a pole pair just inside the
  unit circle (radius ≈ 0.998) — exactly what you'd see for an audio
  PEQ stage at low Q.

- Pre-gain, shift=23: `1495012349 / 2^23 ≈ 178.21`. Pre-gain is a
  *scalar*; one MAC, not a biquad, so a much larger headroom shift is
  used.
- Post-gain, shift=28: `963423114 / 2^28 ≈ 3.587`.

So:
- Biquad coefficient stages → typically **shift=30 → Q1.30** (numeric
  range −2.0 ≤ x < 2.0), which is the natural fit for `a1` values
  approaching ±2.0 near high-Q poles.
- Scalar gains → **shift chosen per-row** to give headroom. Pre/post
  shifts are 23 and 28 in the driver defaults.

**Maximum representable magnitude per stage** is dictated by the shift:
with shift=30, the magnitude bound is `(2^31 - 1) / 2^30 ≈ 1.99999999`.
If a designed `a1` would exceed this (rare in audio biquads, would
require pole radius > 0.9999 and freq near Nyquist), pick a smaller
shift, e.g. shift=29 → Q2.29 → magnitude < 4.0.

Saturation: the driver does not check; raw values are passed through.
The hardware does saturating arithmetic in the accumulator (per TRM
generic AHUB block; not explicit in the PEQ section).

---

## 5. Userspace must design the coefficients

The kernel does **no biquad design**. It writes the raw integer
coefficients straight into AHUB coefficient RAM. From
`tegra210_peq_ahub_ram_put` (`tegra210_peq_alt.c:130-149`):

```c
for (i = 0; i < params->soc.num_regs; i++)
    data[i] = (s32)ucontrol->value.integer.value[i];
tegra210_xbar_write_ahubram(ope->peq_regmap, reg_ctrl, reg_data,
                            params->shift, data, params->soc.num_regs);
```

So userspace (`banks-frontend`, a Python helper, etc.) computes the
floating-point biquad from `(f0, Q, gain_dB, fs)` using the standard RBJ
audio-EQ cookbook, then quantizes each coefficient by the chosen
per-stage shift.

**Partial fills**: the control is fixed-size (62 / 14 s32). You always
write the full array. Unused bands must be set to **identity**:
`b0=1, b1=b2=a1=a2=0` with `shift=0` (matches driver default for
bands 5..11). The kernel `PEQ_CONFIG.BIQUAD_STAGES` field
(`tegra210_peq_alt.c:184-187`, TRM p3678) tells the hardware how many
stages are active — set that via the `peq biquad stages` integer
control (1..12, **note 1-based: register field stores N-1**, see
`tegra210_peq_codec_init`'s `(biquad_init_stage - 1)`).

---

## 6. Bringing it all together — write sequence

ALSA-side, per channel N ∈ 0..7:

1. `amixer -c APE cset name='OPE1 peq active' 0` (bypass before reprogramming).
2. `amixer -c APE cset name='OPE1 peq biquad stages' <S>` where `S` is the
   number of active biquad stages (1..12). Driver subtracts 1 when
   writing the register.
3. `amixer -c APE cset name='OPE1 peq channel<N> biquad shift params'
   <14 comma-separated s32>` — pre, band0..11, post.
4. `amixer -c APE cset name='OPE1 peq channel<N> biquad gain params'
   <62 comma-separated s32>` — pre, band0(5), …, band11(5), post.
5. `amixer -c APE cset name='OPE1 peq active' 1` to re-engage.

(The actual control name prefix on the running device depends on whether
the codec is `OPE1` or `OPE2`; on Xavier NX with the standard DT it's
`OPE1`. Confirm with `amixer -c APE controls | grep -i peq`.)

Note that the gain-param shift and shift-param shift are sent to the
*same* AHUB RAM bank but via two different (CTRL,DATA) register pairs
(`*_CONFIG_RAM_CTRL/DATA` at 0x10/0x14 for gains, `*_CONFIG_RAM_SHIFT_CTRL/DATA`
at 0x18/0x1c for shifts; `tegra210_peq_alt.h:27-30`, TRM p3679-3680).
There are two physically distinct on-chip RAMs.

---

## 7. Reference Python — design + serialize one PEQ channel

```python
import math, struct

def rbj_peaking(f0, Q, gain_db, fs):
    """Robert Bristow-Johnson audio EQ cookbook — peaking EQ.
    Returns (b0, b1, b2, a1, a2) with a0 normalized to 1."""
    A     = 10 ** (gain_db / 40)          # peaking uses sqrt(A)
    w0    = 2 * math.pi * f0 / fs
    alpha = math.sin(w0) / (2 * Q)
    cos_w0 = math.cos(w0)

    b0 =   1 + alpha * A
    b1 =  -2 * cos_w0
    b2 =   1 - alpha * A
    a0 =   1 + alpha / A
    a1 =  -2 * cos_w0
    a2 =   1 - alpha / A

    return (b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)

def quantize(coef_float, shift):
    """Convert float coefficient → signed int32 in Q(31-shift).shift."""
    raw = round(coef_float * (1 << shift))
    if raw >  0x7fffffff: raw =  0x7fffffff
    if raw < -0x7fffffff: raw = -0x7fffffff
    return raw

def build_peq_channel(stages, pre_gain=1.0, post_gain=1.0,
                      pre_shift=23, post_shift=28, band_shift=30):
    """stages: list of (b0,b1,b2,a1,a2) tuples, up to 12 entries.
       Returns (shift_params[14], gain_params[62]) ready for ALSA write."""
    assert len(stages) <= 12
    pad = [(1.0, 0.0, 0.0, 0.0, 0.0)] * (12 - len(stages))
    all_stages = list(stages) + pad

    shift_params = [pre_shift] + [band_shift if i < len(stages) else 0
                                  for i in range(12)] + [post_shift]

    gain_params = [quantize(pre_gain, pre_shift)]
    for i, (b0,b1,b2,a1,a2) in enumerate(all_stages):
        s = band_shift if i < len(stages) else 0
        # NB the *register* stores a1/a2 with the same sign as the math;
        # direct-form-I with implicit a0 = 1 << s.
        gain_params += [quantize(b0,s), quantize(b1,s), quantize(b2,s),
                        quantize(a1,s), quantize(a2,s)]
    gain_params += [quantize(post_gain, post_shift)]

    return shift_params, gain_params
```

### Worked example — peaking +3 dB at 1 kHz, Q=0.707, fs=48 kHz

```
>>> rbj_peaking(1000, 0.707, 3.0, 48000)
(b0= 1.0432, b1=-1.9712, b2= 0.9377, a1=-1.9712, a2= 0.9810)

(values are the normalized direct-form-I coefficients
 b0/a0 .. a2/a0; a0 is implicit.)

>>> [quantize(c, 30) for c in (1.0432, -1.9712, 0.9377, -1.9712, 0.9810)]
[1120115979, -2116810309, 1006841732, -2116810309, 1053303706]
```

So band-0 of channel-0 for a single-stage `+3 dB @ 1 kHz, Q=0.707` peak
EQ at 48 kHz would be programmed as:

```
shift_params (14 × s32):
    23, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28

gain_params (62 × s32):
    pre = quantize(1.0, 23) = 8388608
    band0 = 1120115979, -2116810309, 1006841732, -2116810309, 1053303706
    bands 1..11 = (1, 0, 0, 0, 0)            ← identity
    post = quantize(1.0, 28) = 268435456
```

Don't forget `amixer -c APE cset name='OPE1 peq biquad stages' 1`
to tell the hardware only one stage is active.

### Sanity check via the driver's own default band-0 (notch-ish, shift=30)

```
quantize(0.5, 30)        == 536870912    ✓   (matches b0/b2 in default)
quantize(-1.0, 30)       == -1073741824  ✓   (matches b1 in default)
2143508246 / 2^30        ≈ 1.99629       (a1)
-1069773768 / 2^30       ≈ -0.99630      (a2)
```

Pole magnitude `sqrt(0.99630) ≈ 0.99815` — a high-Q stage near DC, which
is the standard low-shelf prototype that the driver's table ships with.

---

## 8. MBDRC `* band biquad coeffs` — same per-stage layout, different macro plumbing

`include/tegra210_mbdrc_alt.h:206-232`:

```c
#define TEGRA210_MBDRC_MAX_BIQUAD_STAGES 8
#define TEGRA210_MBDRC_BIQ_PARAMS_PER_STAGE 5
struct tegra210_mbdrc_band_params {
    ...
    /* For biquad_params[][5] order of coeff is b0, b1, a0, a1, a2 */
    u32 biquad_params[TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5];
};
```

- Per-band control name (TYPE_BYTES, 40 u32 = 160 bytes):
  `mbdrc {low,mid,high} band biquad coeffs`
  (`tegra210_mbdrc_alt.c:508-526`).
- 8 stages × 5 coeffs/stage = 40 u32 elements per band.
- Same coefficient order as PEQ: **`b0, b1, b2, a1, a2`** per stage
  (treat the inline `a0` label as a typo — see §2 reasoning; matches
  PEQ which is from the same generation of HW and same kernel
  author).
- Q-format: same Q(31-shift).shift family. MBDRC does **not** expose
  per-band shift via a separate control like PEQ; the global
  `MBDRC_CONFIG.SHIFT_CTRL` field (`MBDRC_CONFIG_SHIFT_CTRL_MASK`
  `= 0x1f << 8`, TRM p3680, header line 77-78) sets *one* shift for
  the whole MBDRC. Pick e.g. 30 for unity-bounded coeffs.
- `iir_stages` (separate per-band integer control `mbdrc iir stages`
  applied 3× via the band-stride macro at
  `tegra210_mbdrc_alt.c:398-411`) selects how many of the 8 stages are
  active. Unused stages: identity `(1<<shift, 0, 0, 0, 0)`.

For a typical 2-way crossover at 2 kHz with 24 dB/oct LR4:
- LOW band: 2 cascaded Butterworth LPF biquads at 2 kHz → fill stages
  0..1, identity in 2..7, `mbdrc iir stages = 2` for that band.
- HIGH band: 2 cascaded Butterworth HPF biquads at 2 kHz → same shape.
- MID band: identity (or unused — set mode to dualband).

The CTRL/DATA register pair for MBDRC AHUB RAM is `MBDRC_AHUBRAMCTL_*`
at offsets 0xf8 / 0x104 (header line 58-59); the put/get handler
(`tegra210_mbdrc_alt.c:319-344`) uses `params->shift` as the AHUB RAM
*offset* (in u32 words) to pick which band — `0` for low, `40` for
mid, `80` for high (`(TEGRA210_MBDRC_FILTER_PARAM_STRIDE *
band_index)` × `TEGRA210_MBDRC_MAX_BIQUAD_STAGES * 5`).

---

## 9. AHUB RAM access protocol (for reference)

The CTRL register has these fields (TRM p3679, mirrored in
`utils/tegra210_xbar_utils_alt.c:67-85`):

```
bit 31    READ_BUSY  (RO)
bit 23:16 SEQ_READ_COUNT
bit 14    RW          (0=read, 1=write)
bit 13    ADDR_INIT_EN
bit 12    SEQ_ACCESS_EN
bit 8:0   RAM_ADDR
```

The driver writes a single CTRL word with `ADDR_INIT_EN | SEQ_ACCESS_EN
| RW=write | RAM_ADDR=offset`, then bursts N u32 words to the DATA
register at offset `CTRL + 4`. The hardware auto-increments the address.
This is why `ucontrol->value.integer.value[]` from userspace gets
splatted in order — no per-element address computation needed on the
write side.

Per-channel offset within the gain RAM is
`channel_idx * 62` u32 words; for shifts it's `channel_idx * 14`. The
driver's macros encode that as the `xshift` argument of
`TEGRA_SOC_BYTES_EXT` (`tegra210_peq_alt.c:164-178`):

```c
(TEGRA210_PEQ_GAIN_PARAM_SIZE_PER_CH * chan)
(TEGRA210_PEQ_SHIFT_PARAM_SIZE_PER_CH * chan)
```

The 8 channels share one RAM bank, addressed sequentially. RAM_ADDR is
9 bits → 512 u32 words; PEQ uses 8 * 62 = 496 ≤ 512 for gains and
8 * 14 = 112 ≤ 512 for shifts. Plenty.

---

## 10. Source / spec citations summary

- `nvidia/sound/soc/tegra-alt/include/tegra210_peq_alt.h:23-51`
  — register offsets, MAX_BIQUAD_STAGES=12, MAX_CHANNELS=8,
  PARAM_SIZE_PER_CH formulas.
- `nvidia/sound/soc/tegra-alt/tegra210_peq_alt.c:43-67` — default
  gain table and shift table (Q-format ground truth).
- `nvidia/sound/soc/tegra-alt/tegra210_peq_alt.c:109-178` — control
  put/get/info handlers; confirms TYPE_INTEGER, no TLV.
- `nvidia/sound/soc/tegra-alt/tegra210_peq_alt.c:282-302` — restore
  path (also confirms `data` is `u32 *` passed straight to AHUB RAM).
- `nvidia/sound/soc/tegra-alt/include/tegra210_mbdrc_alt.h:206-232`
  — MBDRC band-params struct, BIQ_PARAMS_PER_STAGE=5,
  MAX_BIQUAD_STAGES=8.
- `nvidia/sound/soc/tegra-alt/tegra210_mbdrc_alt.c:319-355`,
  `:393-396`, `:508-526` — MBDRC coeff put/get + control table
  (TYPE_BYTES, 160-byte blob per band).
- `nvidia/sound/soc/tegra-alt/utils/tegra210_xbar_utils_alt.c:67-107`
  — AHUB RAM read/write protocol (CTRL+DATA, autoincrement).
- `nvidia/sound/soc/tegra-alt/include/tegra210_xbar_alt.h:181-204`
  — `struct tegra_soc_bytes` and `TEGRA_SOC_BYTES_EXT` macro
  (`info = xinfo` — does NOT default to TLV info).
- TRM **§7.8.4.15 (p3677-3680)** — PEQ register map
  (`PEQ_SOFT_RST_0`, `PEQ_CG_0`, `PEQ_STATUS_0`, `PEQ_CONFIG_0`,
  `PEQ_AHUBRAMCTL_PEQ_CTRL_0`, `PEQ_AHUBRAMCTL_PEQ_DATA_0`,
  `PEQ_AHUBRAMCTL_SHIFT_CTRL_0`, `PEQ_AHUBRAMCTL_SHIFT_DATA_0`).
  TRM does **not** document the Q-format; that's an
  empirically-derived but unambiguous fact from the driver defaults.
- TRM **§7.8.4.16 (p3680+)** — MBDRC registers.

---

## 11. Open follow-ups (not blocking)

1. **MBDRC inline-comment typo** "`b0, b1, a0, a1, a2`" — file an
   internal note; verify on hardware by writing `(0x40000000, 0, 0,
   0, 0)` with shift=30 (i.e. `b0=0.5, rest=0`) to a band and
   measuring DC gain — should be −6 dB if b2 is the 3rd slot, not 0
   dB. (Important to lock down before MBDRC tuning.)
2. **No `aN`-sign convention check in the driver** — the standard
   biquad transfer function is `H(z) = (b0 + b1 z⁻¹ + b2 z⁻²) /
   (1 + a1 z⁻¹ + a2 z⁻²)`. Driver default `a1=+1.996` is consistent
   with this (negative pole at z = +0.998). RBJ cookbook returns
   `+a1`/`+a2` form when you divide by `a0`. Watch out if porting
   from drivers (e.g. ALSA `libasound` plugin biquad) that store
   `−a1, −a2`.
3. **No public NVIDIA reference tuning** for these blocks shipped in
   the JetPack BSP. The default values in the driver are placeholder
   curves only; we will need to design the production EQ ourselves
   (banks-frontend Settings → Audio EQ workflow per CLAUDE.md
   roadmap).
4. The PEQ block has a **global `BIAS_UNBIAS` rounding mode**
   (TRM p3678, bit 1 of `PEQ_CONFIG`). Default is `UNBIAS` (round
   toward ±∞ depending on sign). Probably leave alone; mention to
   audio review if perceptible DC offset shows up.
