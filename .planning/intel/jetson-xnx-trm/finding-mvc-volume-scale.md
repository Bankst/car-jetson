# Tegra194 MVC volume control — decoded

Investigation of `MVC1 Volume` (numid=1111) and per-channel siblings on the
Xavier NX devkit. Goal: definitive integer → dB mapping, unity-gain value,
per-channel ↔ master interaction, ramp rate, and a sane default for
`banks-audio-route.sh`.

## TL;DR

| Question | Answer |
|---|---|
| Integer range | `[0, 16000]` (ALSA ctl), in **hundredths of a dB** when curve = Linear |
| Curve = **Linear** (default) | `dB = (int/100) − 120` → range **−120 dB to +40 dB**, **unity = 12000** |
| Curve = **Poly** | `int/100` is a **linear amplitude percent 0..100**; ALSA range collapses to 0..10000, unity = 10000 |
| Unity gain (Linear) | **12000** (= 0 dB). Our current `banks-audio-route.sh` default of `12000` is already correct. |
| Per-channel vs master | Mutually exclusive register modes. Writing master fans out to all 8 channels and disables per-chan; writing a `ChannelN` enables per-chan and the master read-back reflects channel 1 only. |
| HW ramp | `duration = 150 samples` → **≈3.13 ms @ 48 kHz** total transition between any two settings (hardware-interpolated). Curve shape: Linear or Windows-style poly. |
| Recommendation for our script | Keep **12000** (0 dB). If headroom for streamed content that's already near 0 dBFS is desired, set **11000 (−10 dB)** or lower. **Do not exceed 12000** for hardware/listener safety — anything above is positive gain and risks digital clipping in downstream ADMAIF/I2S. |

## 1. Integer → dB mapping (Linear curve, the default)

Source: `sound/soc/tegra/tegra210_mvc.c:162-176` (`tegra210_mvc_conv_vol`) and
`sound/soc/tegra/tegra210_mvc.c:147-156` (`tegra210_mvc_get_vol`):

```c
/* PUT: ALSA int → register */
val -= 12000;
mvc->volume[chan] = (val * (1<<8)) / 100;   /* register = (dB×100 − 12000) × 256/100 */

/* GET: register → ALSA int */
val = (val * 100) >> 8;
val += 12000;
```

Solve: the ALSA integer is **hundredths of a dB, biased by +12000 (i.e. +120 dB)**.

- `dB = (ALSA_int / 100) − 120`
- `register = ((ALSA_int − 12000) * 256) / 100` — this is the hardware **gain register in Q8.24 dB** format (per TRM page 2878–2879: *"Gain value's data formats: Q16.16 (2's complement). Range: -32768 to 32768-2-16"* — driver actually writes the low 24 bits as fractional dB; the kernel comment at line 165–167 says *"-120dB to +40dB (Q8)"*). 
- Clamp by `SOC_SINGLE_EXT(... 0, 16000, ...)` at `tegra210_mvc.c:541`/`552` — ALSA core enforces `[0..16000]`.

| ALSA int | dB | Meaning |
|---|---|---|
| 0 | −120 dB | floor (effectively mute) |
| 6000 | −60 dB | very quiet |
| 11000 | −10 dB | -10 dBFS headroom |
| **12000** | **0 dB** | **unity gain** |
| 13000 | +10 dB | boost (risk clipping) |
| 16000 | +40 dB | max boost (will almost certainly clip) |

Live verification on devkit (`amixer -c APE cget numid=1111`): writes of 0,
12000, 16000 all read back exactly. Default boot value is 12000 (matches
driver: `INIT_VOL_DEFAULT_LINEAR = 0x00000000`, which back-converts to
`(0 * 100) >> 8 + 12000 = 12000`).

## 2. Curve = Poly (non-default)

Source: `tegra210_mvc.c:151-158` (get), `168-175` (put), header default
`TEGRA210_MVC_INIT_VOL_DEFAULT_POLY = 0x01000000`.

- Driver clamps input to **10000 max**.
- Mapping: `register = ((ALSA_int * 256 / 100) << 16)`; this is **linear
  amplitude percent**, 0..100, written into a Q8.24 amplitude word.
- Default `0x01000000` reads back as `100` (= 100%, i.e. unity).
- Live confirmation: after `cset numid=1114 0` (Poly), `cget numid=1111` →
  `100`.

The poly curve uses 9 polynomial coefficients (Windows-audio fade curve)
loaded into RAM at hw_params (`poly_coeff[0..8]` in `tegra210_mvc.c:709-717`).
It is meant for user-perceived linear fade UI sliders (0–100%), not for
precise dB control.

**For our use case (cabin audio mixing, dB-aware headroom budget): stay on
Linear curve (default).**

## 3. Per-channel vs master

Source: `tegra210_mvc.c:221-254` (`tegra210_mvc_put_vol`).

- `MVC1 Volume` (numid=1111) writes `TEGRA210_MVC_TARGET_VOL` (channel 0
  offset). It then **disables** `PER_CHAN_CTRL_EN` and copies the value into
  `mvc->volume[1..7]` plus writes `INIT_VOL` + `TARGET_VOL` for all channels.
- `MVC1 ChannelN Volume` (numid=1103..1110) writes `TARGET_VOL +
  N*REG_SIZE` and **enables** `PER_CHAN_CTRL_EN`. Master read-back then
  reflects channel 0 only (since the master ctl simply reads channel-0
  register).
- Modes are mutually exclusive (`PER_CHAN_CTRL_EN` is a single CTRL bit).
- Live verification: writing `Channel1=14000, Channel2=6000` leaves
  Channel3..8 at the previously-master-set 12000 (chan-3..8 retained from
  the prior master broadcast). Writing master=12000 again resets all 8
  channels to 12000 and turns per-chan back off.

**Implication for `banks-audio-route.sh`**: use the master `MVC1 Volume`
(numid=1111) for any global cabin volume. Per-channel ctls are only useful
if we later need per-speaker trim (e.g. front-L pad vs front-R) — at which
point the script would need to re-broadcast on every "master" change to
avoid drift.

## 4. Curve-type ctl

`numid=1114 'MVC1 Curve Type'`, enum `Poly | Linear`, default `Linear`.
Cannot be changed while MVC is running (`tegra210_mvc.c:320-325`); a switch
resets all 8 channel volumes to the curve's default (Poly→`0x01000000`,
Linear→`0x00000000`).

## 5. HW ramp rate

`mvc->duration = 150` (samples) and `mvc->duration_inv = 14316558` set at
probe (`tegra210_mvc.c:707-708`).

TRM page 2879: `Duration_inv_1byN3 = (1/N3) * 2^prescalar * 2^31`,
prescalar=6. Sanity-check: `(1/150) * 64 * 2^31 = 916,756,901 ≈ 916,257,829`
— the driver value 14,316,558 is `(1/150) * 2^31 / 10` ish; actually
`14316558 ≈ 2^31 / 150 = 14,316,557.9`. So **prescalar appears to be 0 in
this driver** (`2^31 / 150`), not 6. Either way:

- `duration = 150 samples` is the number of **sample clocks** the hardware
  takes to interpolate from `INIT_VOL` to `TARGET_VOL` after any
  `VOLUME_SWITCH` trigger.
- At 48 kHz: `150 / 48000 ≈ 3.125 ms` per transition.
- At 44.1 kHz: ≈ 3.40 ms.
- At 192 kHz: ≈ 781 µs.

This is **why we don't need a software ramp on top of MVC writes** — unlike
the `param_eq` filter-chain biquads (which click on coefficient change), MVC
hardware interpolates target volume natively. A single
`amixer cset numid=1111 <new>` will be inaudibly smooth.

Curve shape during the 3 ms ramp:
- Linear curve: straight-line dB interpolation.
- Poly curve: 9-coefficient Windows-audio fade curve from RAM.

(Neither shape is configurable from the ALSA ctl set — they're hard-coded
to match the selected curve type.)

## 6. Other related ctls (for completeness)

| numid | Name | Behavior |
|---|---|---|
| 1112 | `MVC1 Mute` | BOOLEAN. Sets `MUTE` bit in CTRL; affects channel 0 only when per-chan-mute is disabled (default). |
| 1113 | `MVC1 Per Chan Mute Mask` | INTEGER 0..255. Bitmask per channel; setting any value enables per-channel mute mode. |
| 1115 | `MVC1 Bits` | Overrides ACIF bit width (8/16/24/32 — sample-width forcing). Leave 0 unless we hit a bit-depth mismatch. |
| 1116 | `MVC1 Audio Channels` | Forces ACIF channel count override. 0 = follow stream. |
| 1117 | `MVC1 Audio Bit Format` | Enum `None | 16 | 32`. Input-side ACIF override. |
| 1118 | `MVC1 Bypass` | BOOLEAN. Hardware bypass — pass-through with no gain logic. Useful for measurement / diagnostics. |
| 1301 | `MVC1 Mux` | XBAR input source selector for the MVC1 RX port. |

## 7. Recommended default for `banks-audio-route.sh`

The current value `12000` corresponds to **exactly 0 dB unity gain** — i.e.
the MVC is transparent. This is the safe default; it lets all upstream gain
budgeting happen in WirePlumber / PipeWire / source apps with the hardware
volume control adding no insertion gain or attenuation.

**Keep `12000`.** Recommended alternative defaults if a use case appears:

- **11500 (−5 dB)** — small headroom margin for streamed content that
  occasionally peaks above 0 dBFS post-source-side limiter; minimal audible
  attenuation but eliminates ADMAIF clipping risk.
- **11000 (−10 dB)** — conservative "max safe with no source-side limiter".
- **Avoid > 12000** — positive MVC gain only makes sense if the source is
  intentionally below 0 dBFS and you've measured peak headroom; otherwise
  it clips at the I2S/HDA boundary.

For mute, prefer `numid=1112 'MVC1 Mute' on` (uses HW mute, ramped) over
writing volume = 0 (also works but bypasses the mute-state semantics).

## Citations

- Driver: `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/sound/soc/tegra/tegra210_mvc.c:147-176, 221-254, 320-325, 538-553, 705-718`
- Header: `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/sound/soc/tegra/tegra210_mvc.h` (defines `TEGRA210_MVC_INIT_VOL_DEFAULT_*`, `TEGRA210_MVC_MAX_CHAN_COUNT=8`, `REG_SIZE=4`)
- TRM Xavier 1.4p:
  - Page 2851 — MVC overview, ramp/curve features (TRM.txt:175366-175395)
  - Page 2878-2879 — Gain RAM data formats: poly coeffs Q8.24, gain Q16.16 (TRM.txt:176765-176766)
  - Page 2885-2886 — MVC programming guidelines, INIT_VOL semantics, VOLUME_SWITCH trigger (TRM.txt:177063-177126)
- Live device: devkit at `root@192.168.55.1`, kernel `5.10.216-l4t-r35.6.4`, module `snd-soc-tegra210-mvc`.
