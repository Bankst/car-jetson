# Speaker Protection Options — Banks Jetson Linux

**Status:** investigation, no code changes  
**Target:** `jetson-xavier-nx-banks-devkit` driving PCM5102A I2S DAC via AHUB OPE1 PEQ/MBDRC today; eventual cabin speakers via external Class-D amp  
**Audience:** future-me / anyone tuning the audio graph for real speakers

---

## Part A — Tegra194 hardware: is SPKPROT1 a usable HW block?

### Verdict: **DEAD on this image. Effectively DEAD on L4T R35 in general.**

The `SPKPROT1` endpoint visible in the AHUB part-mux is a **routing-only AHUB
shim that forwards frames to an ADSP-resident plugin (`nvspkprot.elf`)**. There
is no Tegra-side silicon DSP for speaker protection independent of the ADSP.
On our image the ADSP plugin doesn't load, so SPKPROT1 routes traffic to a
black hole.

### Evidence

**1. No matching kernel driver for `nvidia,tegra210-spkprot`.**

DT node (Xavier T194 SoC dtsi):

```
build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/
  nvidia/soc/t19x/kernel-dts/tegra194-soc/tegra194-soc-audio.dtsi:276
    tegra_spkprot: spkprot@2908c00 {
        compatible = "nvidia,tegra210-spkprot";
        reg = <0x0 0x2908c00 0x0 0x400>;
        nvidia,ahub-spkprot-id = <0>;
        status = "disabled";
    };
```

Override in platform enable dtsi flips `status = "okay"`:

```
nvidia/platform/tegra/common/kernel-dts/audio/tegra-platforms-audio-enable.dtsi:44
    spkprot@2908c00 { status = "okay"; };
```

But: **no `.c` file anywhere in the tree binds `nvidia,tegra210-spkprot`** —
`grep -rn "tegra210-spkprot" sound/soc/tegra/ sound/soc/tegra-alt/ nvidia/sound/`
yields zero matches outside DTSI. Nothing in the mainline AHUB driver
(`sound/soc/tegra/tegra210_ahub.c`) or the legacy alt driver
(`nvidia/sound/soc/tegra-alt/`) probes the SPKPROT register window at
`0x2908c00`. Compare against neighbours — `tegra210_ope.c`, `tegra210_peq.c`,
`tegra210_mbdrc.c`, `tegra210_mvc.c`, `tegra210_sfc.c`, `tegra210_amx.c` — all
have full register-poke drivers. SPKPROT has none.

**2. The DT *expects* an ADSP plugin to back it.**

```
nvidia/platform/tegra/common/kernel-dts/audio/tegra-platforms-audio-dai-links.dtsi:29
    plugin-info-2 {
        plugin-name = "spkprot";
        firmware-name = "nvspkprot.elf";
        widget-name = "SPKPROT-SW";
    };
```

The widget that AHUB users wire to is named `SPKPROT-SW` (`-SW` = software /
ADSP-hosted). The `SPKPROT1` entry in the AHUB mux is one end of the routing
pair; the ADSP plugin is the other. Without the plugin loaded, audio routed
into `SPKPROT1` has no consumer.

**3. ADSP plugin does not load on this image** (already established by prior
   validator; symptom: "Failed to init app" for `nvspkprot.elf`). The shipped
   `adsp-fw.bin` (392 KB, encrypted/compressed — strings yields nothing) only
   exposes the `WIRE` plugin + framework helpers. NVIDIA does not ship
   `nvspkprot.elf` as an extractable artefact for L4T R35.

**4. TRM (Xavier_TRM_DP09253002_v1.4p) has no SPKPROT functional chapter.**

`refs/Xavier_TRM_DP09253002_v1.4p.pdf` mentions SPKPROT1 only in three
contexts:

- p38 (address map): `SPKPROT1  0x02908c00 – 0x02908fff  SYSTEM` (1 KB window)
- AHUB part-mux tables: `AXBAR_PART_*_SPKPROT1_RX1_0` (source mux) +
  `SPKPROT1_TX1` (sink mux) — pure routing fabric
- AHUB clear-control tables: SPKPROT1 can be CLEAR'd like any other AHUB block

There is **no chapter describing SPKPROT registers, control bits, coefficients,
thermal/excursion models, or per-channel limiter behavior**. The TRM treats
SPKPROT as a black box you reach via routing — implementation is in software
(the ADSP plugin) that NVIDIA did not document publicly.

This is consistent with the public NVIDIA position that ADSP DSP algorithms
(SPKPROT, SRC, AAC-DEC, MP3-DEC, AEC) are partner-licensed and that the only
piece of the algorithm graph the OEM is meant to touch is via the ADSP plugin
ABI — which itself isn't open and we don't have a working build of.

**5. Even if we got the plugin loaded, the ALSA control surface is the
   useless ADSP triad.**

The only controls exposed by the `tegra210_adsp_alt.c` machine driver for a
loaded plugin are `<widget> set params`, `<widget> send bytes`, and `<widget>
get bytes`. There is no parametric attack-time/release/threshold/gain
control like there is for PEQ (`OPE1 PEQ Coeff Data`) or MBDRC (`MBDRC <field>`).
Tuning would require generating opaque parameter blobs out-of-band that
match NVIDIA's undocumented plugin ABI — not feasible without NVIDIA support.

### Live probe (deferred — target offline at investigation time)

Target `192.168.55.1` was unreachable during this investigation. When the
board is back:

```sh
ssh root@192.168.55.1 'amixer -c APE controls | grep -i spkprot'
# Expected (based on virtual-alt xbar source): only "SPKPROT1 Mux"
# i.e. the routing mux selecting which source feeds SPKPROT1's RX1.
# No SPKPROT-specific param controls.
```

If you want to round-trip-test, you can attempt:

```sh
amixer -c APE cset name='SPKPROT1 Mux' 'OPE1'
amixer -c APE cset name='I2S5 Mux' 'SPKPROT1'
# play a tone, then sweep amplitude/frequency — if SPKPROT acts as a
# silicon DSP, you'd see clipping/limiting on the I2S output.
# Predicted result: silence (no consumer in ADSP), or pass-through with no
# limiting (if AHUB silently bypasses the dead endpoint).
# Restore: `banks-audio-route reset` (or rerun without INSERT_SPKPROT).
```

This test is **NOT REQUIRED to reach the verdict** — the lack of a kernel
driver for the SPKPROT register window settles it. SPKPROT1 cannot do
silicon-side processing because the only code path that knows the register
layout is the ADSP firmware blob, which on R35 we cannot load on Xavier NX
in our build configuration.

### Why this differs from PEQ / MBDRC

PEQ and MBDRC are sub-blocks of OPE1 with their own kernel drivers
(`tegra210_peq.c`, `tegra210_mbdrc.c`) that own the register windows
`0x2908100` / `0x2908200` and expose ALSA controls
(`OPE1 PEQ Coeff Data`, `MBDRC peak rms mode`, etc.). They run in pure
silicon — no ADSP involvement. SPKPROT, despite living at a nearby address
(`0x2908c00`) inside the same AHUB partition, was architected as an ADSP-
hosted plugin from day one.

---

## Part B — Realistic SW speaker protection for PipeWire

Given SPKPROT is dead, all speaker protection must run on the Carmel CPU side
of PipeWire. The good news: at 48 kHz, a per-channel limiter + DC block + HPF
is cheap. The annoying news: PipeWire 1.0.9's builtin filter-chain library
ships *only* static IIRs + clamp — no envelope follower, no compressor, no
brick-wall limiter. So we need either LADSPA plugins or an external SW DSP
process inserted in the graph.

### B.1 — PipeWire 1.0.9 builtin filter-chain inventory (authoritative)

Source-of-truth: `module-filter-chain/builtin_plugin.c` from our actual
PipeWire 1.0.9 git tree under `build/tmp/work/.../pipewire/1.0.9/git/`.

Builtins shipped:

| Label | Purpose | Useful for SP? |
|---|---|---|
| `copy` | 1:1 passthrough | bypass scaffolding |
| `mixer` | 8-in 1-out, per-input Gain | summing post-EQ |
| `bq_lowpass` | biquad LP | crossover/cone protection |
| `bq_highpass` | biquad HP | **sub-Fs / DC protection (primary SP tool)** |
| `bq_bandpass` | biquad BP | freq-weighted excursion detector |
| `bq_lowshelf` | biquad lowshelf | bass shaping |
| `bq_highshelf` | biquad highshelf | treble shaping |
| `bq_peaking` | parametric peak/notch | bands of `param_eq` |
| `bq_notch` | biquad notch | hum/rattle removal |
| `bq_allpass` | biquad APF | phase alignment |
| `bq_raw` | direct b0..a2 coefficient load | tuned-by-Octave biquads |
| `convolver` | FFT FIR convolver via pffft | room/speaker correction IR |
| `delay` | sample-aligned delay | crossover time-align |
| `invert` | sign flip | polarity flip |
| `clamp` | hard clip to [min,max] | **only available "limiter" — instantaneous, hard, no lookahead** |
| `linear` | scale+offset | gain trim |
| `recip`, `exp`, `log`, `mult`, `sine` | math control nodes | sidechain-ish wiring |

There is also a `param_eq` *node-template* (not a separate label) that
internally chains N biquads — what we already use for the 8-band cabin EQ.

**Notable absences** in 1.0.9:

- No `dcblock` (added in PW ~1.2).
- No `compressor` (added in PW 1.4).
- No `limiter` / `lookahead_limiter` (added in PW 1.4).
- No `envelope_follower`. There is no time-varying gain primitive at all
  beyond `clamp` and the math nodes (which can't keep state across blocks).

**CLAUDE.md correction:** the previous notes claim PW 1.0.9 ships `dcblock`.
It does not. (Verified via `grep -rn dcblock build/tmp/work/.../pipewire/1.0.9/git/src/modules/` → zero hits.)

### B.2 — Filter-chain *can* host LADSPA (and not LV2)

`module-filter-chain` loads three plugin backends in our build:

- `builtin_plugin.c` — always built
- `ladspa_plugin.c` — always built (no meson option; just needs `libdl`)
- `lv2_plugin.c` — gated on `-Dlv2=enabled`, **disabled in our build**
  (`meta-multimedia/recipes-multimedia/pipewire/pipewire_1.0.9.bb` line 62)
- `sofa_plugin.c` — disabled (`-Dlibmysofa=disabled`)

If we want LV2 (lsp-plugins-lv2 is the strongest open-source SP suite), we'd
need to:

1. add `lilv` + `lv2` recipes (not in meta-oe scarthgap),
2. flip `-Dlv2=enabled` via a `pipewire_%.bbappend` PACKAGECONFIG line.

Both feasible but a non-trivial rabbit hole. LADSPA is the cheap path.

### B.3 — LADSPA / LV2 packages available in our layer set

`grep`'d every `*.bb` under `meta-openembedded` and `poky`:

| Plugin set | Recipe path in our layers | Notes |
|---|---|---|
| **CAPS 0.9.26** (LADSPA) | `meta-openembedded/meta-multimedia/recipes-multimedia/caps/caps_0.9.26.bb` | only LADSPA suite shipped; installs `caps.so` at `${libdir}/ladspa/` |
| swh-plugins (LADSPA) | not present | upstream meta-oe has never had it; would need a custom recipe |
| swh-lv2 | not present | needs LV2 host first |
| Calf LV2 | not present | needs LV2 host |
| lsp-plugins (LV2+LADSPA) | not present | best limiter suite open-source; needs new recipe |
| easyeffects | not present | also pulls a huge dep tree |
| alsa-equal | `meta-multimedia/recipes-multimedia/alsa-equal/alsa-equal_0.6.bb` | alsa-side PEQ, not relevant |

CAPS plugins relevant to SP (from CAPS docs):

- **`Compress`** (LADSPA ID 1772) — compressor + soft-saturating limiter,
  peak/RMS detection, optional oversampling. Closest thing to a peak limiter
  CAPS ships.
- **`CompressX2`** (ID 2598) — stereo coupled compressor.
- **`Saturate`** (ID 1771) — soft clipping, has a "clip" preset usable as a
  rough brick-wall.
- **`Noisegate`** (ID 2602) — useful for cabin mic, not for speaker out.
- **`Eq10`**, **`EqFA4p`**, **`ToneStack`** — biquad-based, already covered
  by our `param_eq`.

CAPS gets us a single-band soft-saturating compressor + a clip-style
saturator. That is *enough* for a v1 protection net behind our existing PEQ
graph. It is **not** a true brick-wall limiter (no lookahead) and has no
explicit thermal/excursion model. For v2 we'd want lsp-plugins.

### B.4 — Reference SP algorithm

Industry-standard speaker-protection chain (top to bottom in the signal flow,
post-EQ, pre-DAC):

1. **DC blocker** — first-order HP at ~5 Hz, mandatory for I2S DACs feeding
   any moving-coil driver (DC current → coil heat → no acoustic output).
2. **Sub-Fs high-pass (cone-excursion protection)** — second-order Butterworth
   or LR4 HP just below the driver's Fs (typical 60–120 Hz for full-range,
   30–50 Hz for woofers, 200–500 Hz for tweeters in a multiway). Eliminates
   the band where excursion is unbounded.
3. **Frequency-weighted excursion detector + dynamic HPF** (advanced, v2) — a
   bandpass that mimics the driver's mechanical compliance, fed into an
   envelope follower; when energy below Fs spikes, raise the HPF corner
   dynamically. Needs envelope follower → not buildable in PW 1.0.9 alone.
4. **Voice-coil thermal model + RMS limiter** — first-order RC integrator
   on |x|² with τ ~ 1–10 s representing voice-coil heating. When integrator
   exceeds a threshold, apply a slow gain reduction. Saves the coil from
   long-term burnout. Needs envelope follower.
5. **Peak limiter (brick-wall)** — lookahead 1–5 ms, hard ceiling at ~–1 dBFS,
   prevents DAC clipping and amplifier clip-driven HF energy.

What we can build from PW 1.0.9 alone:

| Stage | Buildable from PW builtins? | How |
|---|---|---|
| DC blocker | YES — `bq_highpass` at f≈5 Hz, Q=0.7 | trivial |
| Sub-Fs HPF | YES — `bq_highpass` at f=driver Fs, Q=0.5 (Butterworth) | trivial |
| Excursion detector | NO — needs envelope follower | requires LADSPA `Compress` (sidechained, abused) or external proc |
| Thermal RMS limiter | NO — same reason | same |
| Peak limiter | PARTIAL — `clamp` does hard-clip at ±ceiling but no lookahead, **will introduce HF distortion** | acceptable as a last-resort fail-safe behind a properly tuned compressor |

For a usable v1, the gap (envelope-follower-based dynamics) gets filled by
**CAPS `Compress`** loaded as a LADSPA node in the filter chain.

### B.5 — CPU cost on Carmel @ 48 kHz, 6 channels

Order-of-magnitude estimates, single Carmel core (1 of 6, ~1.9 GHz typ):

| Stage | Cost per ch @ 48k | 6 ch total |
|---|---|---|
| 1 biquad (HPF or PEQ band) | ~30–60 cycles/sample → ~3 MIPS | ~18 MIPS |
| `clamp` | ~5 cycles/sample → ~0.25 MIPS | ~1.5 MIPS |
| CAPS `Compress` (1 band, peak-mode) | ~200–400 cycles/sample → ~15 MIPS | ~90 MIPS (mono) or ~120 MIPS (`CompressX2` stereo-coupled) |
| Convolver 4k-tap FIR (one ch) | dominated by FFT; pffft on Cortex-A57 ~5–8% one core; assume 10–15% on Carmel | not realistic for 6 ch without ARM NEON SIMD bench |

Rough budget for **HPF + DC block + Compress + clamp** per channel:
~20 MIPS/ch → **~120 MIPS for 6 channels ≈ 6% of one Carmel core**. Fine.

Add 8-band `param_eq` (already in place): another ~25 MIPS/ch → still
comfortable. We have headroom.

If we ever wire room-correction convolvers (per-channel 8k-tap FIRs),
budget jumps to maybe 30–40% of one core for 6 ch. Still fine but no
longer trivial.

### B.6 — Recommended v1 filter-chain snippet (PW 1.0.9 + CAPS LADSPA)

This is a *stereo* SP block; replicate the node and wire upstream/downstream
ports for surround. Insert between the current `banks_eq` virtual sink and
the hardware I2S5 sink, so EQ runs first and SP catches everything.

```
# /etc/pipewire/pipewire.conf.d/99-banks-spkprot.conf
context.modules = [
  { name = libpipewire-module-filter-chain
    args = {
      node.description = "Banks Speaker Protection"
      media.name       = "Banks Speaker Protection"
      filter.graph = {
        nodes = [
          # ----- Left channel chain -----
          { type = builtin  label = bq_highpass  name = dcblock_l
            control = { "Freq" = 5    "Q" = 0.707 "Gain" = 0 } }
          { type = builtin  label = bq_highpass  name = subfs_l
            # tune to driver Fs + margin; example: 60 Hz LR2
            control = { "Freq" = 60   "Q" = 0.5   "Gain" = 0 } }
          { type = ladspa   plugin = caps        label = Compress  name = comp_l
            # CAPS Compress params (peak mode, gentle SP curve):
            # threshold ~ -6 dBFS, ratio 8:1, attack 1 ms, release 100 ms,
            # makeup 0 dB, knee soft, oversampling on
            control = { "mode" = 1    "threshold" = -6  "ratio" = 8
                        "attack" = 1  "release" = 100   "makeup" = 0 } }
          { type = builtin  label = clamp        name = brick_l
            # ultimate fail-safe — DAC ceiling, never reached if Compress is tuned
            control = { "Min" = -0.95 "Max" = 0.95 } }

          # ----- Right channel chain — same nodes with _r suffix -----
          ...
        ]
        links = [
          { output = "dcblock_l:Out"  input = "subfs_l:In"  }
          { output = "subfs_l:Out"    input = "comp_l:In"   }
          { output = "comp_l:Out"     input = "brick_l:In"  }
          ...
        ]
        inputs  = [ "dcblock_l:In"  "dcblock_r:In"  ]
        outputs = [ "brick_l:Out"   "brick_r:Out"   ]
      }
      capture.props = {
        node.name      = "banks_spkprot"
        media.class    = Audio/Sink
        audio.channels = 2
        audio.position = [ FL FR ]
      }
      playback.props = {
        node.name      = "banks_spkprot_out"
        node.passive   = true
        target.object  = "alsa_output.platform-sound.stereo-fallback"
      }
    }
  }
]
```

Key choices:

- `bq_highpass` *before* `Compress` — DC and sub-Fs energy removed before
  the compressor sees it, so the compressor isn't constantly chasing
  inaudible bass.
- `clamp` at the very end as a non-musical safety net. Tune Compress so
  `clamp` never fires; if it does, audible distortion is the warning that
  the compressor threshold or ratio is wrong.
- Wired as **passive** sink downstream of `banks_eq`. Chain becomes:
  `app → banks_eq (PEQ) → banks_spkprot (HPF+Compress+clamp) → alsa hw I2S5`.
- All controls are static — no runtime params required. Once tuned per
  driver/amp, drop in as a recipe file.

### B.7 — When we need v2 (lsp-plugins-lv2)

v2 triggers when *any* of these are true:

- Multi-band SP becomes necessary (e.g. tweeter excursion vs woofer thermal
  diverge → CAPS single-band can't represent both).
- The amp has no soft-clip on the input, so we need a real lookahead limiter
  (CAPS Compress has no lookahead → momentary peaks above threshold leak
  through and the `clamp` fail-safe distorts them).
- We add real cabin EQ from measurements (likely wants FIR via convolver,
  which is fine in 1.0.9 builtin, but a real room-correction toolchain
  expects LV2).

For v2: ship `lilv` + `lv2` recipes, flip `-Dlv2=enabled` in pipewire
PACKAGECONFIG, recipe lsp-plugins-lv2 → use `sc_limiter_mono`,
`sc_limiter_stereo`, `mb_limiter_*`. Cost: 1 day to plumb, hours to tune.

---

## Part C — Known unknowns to resolve before SP tuning matters

These all need real answers before the v1 SP config above is more than a
scaffolding placeholder. SP parameters (HPF corner, compressor threshold,
attack/release) are *driver- and amp-specific*; tuning blind is worse than
no SP.

1. **What speaker drivers** are going into the vehicle?
   - Fs (resonance freq) of each driver — sets the sub-Fs HPF corner.
   - Xmax (linear excursion) — sets the excursion-limit threshold.
   - Power handling: long-term thermal (RMS) and short-term peak — sets the
     compressor threshold and the thermal-model time constants.
   - Sensitivity (dB SPL @ 1 W / 1 m) — sets the level at which limiting
     should engage relative to user-set volume.
   - Coil DC resistance + thermal mass — second-order, only matters if
     building the full thermal model in v2.

2. **What amplifier?**
   - Chip / topology (TI TAS5825M, TPA3255, Analog Devices SSM3582, ST
     TDA7498, NXP TFA9874, etc.). Class-D-with-feedback chips behave very
     differently from open-loop ones for SP purposes.
   - **Does the amp already have onboard SP?** TI's TAS5805M / TAS5825M
     family runs a full Smart Amp DSP (process flow incl. peak/excursion/
     thermal limiters) — if we ship one of those, upstream SW SP is largely
     redundant and could even fight the amp's own limiter. NXP TFA98xx
     family has CoolFlux DSP doing similar.
   - Input level / gain — sets the absolute `clamp` ceiling and the
     compressor makeup gain.
   - Amp protection thresholds (clip detect, current limit, thermal foldback)
     — informs what behaviour SW SP should *prevent* triggering.

3. **Enclosure / cabin acoustics**
   - Ported vs sealed enclosure changes the excursion-vs-frequency curve
     (ports unload below tuning → infinite excursion).
   - Cabin transfer function → eventually needs measurement and a per-
     channel FIR (convolver builtin handles this).

4. **Channel layout**
   - 2.0 / 2.1 / 4.0 / multi-zone? Determines how many filter-chain nodes
     and whether SP is per-channel-independent or stereo-coupled.

5. **Volume control architecture**
   - Where does the user volume sit relative to SP? If volume is *after*
     SP, the compressor threshold can be referenced to a fixed digital
     ceiling and tuning is simple. If volume is *before* SP (as it is
     today via `wpctl`), SP needs to be referenced to peak digital, which
     is what the v1 config assumes.

6. **Regulatory / liability posture**
   - Some markets require a documented SP chain for products sold with
     "do not exceed X SPL" warranties. If this matters, the v2 lsp-plugins
     path produces tunable, measurable, repeatable behaviour that's
     defensible. CAPS Compress in v1 is good enough for dev/internal but
     not for a published spec.

---

## Final recommendation

### Where SP lives in the audio graph

```
[ app ] → [ banks_eq (param_eq, 8-band) ] → [ banks_spkprot (HPF + DC + Compress + clamp) ] → [ alsa_output I2S5 ]
                                                                                                       │
                                                                                                       └─ HDMI sink remains separate; SP node is HW-bound to I2S only
```

The SP virtual sink is `node.passive`, targets the I2S5 ALSA sink, and only
intercepts traffic destined for the cabin amp. HDMI / USB / BT-A2DP routes
to other sinks are unaffected.

### What to ship in v1 (now, with PCM5102A breakout)

1. **Forget SPKPROT1.** Remove any `INSERT_SPKPROT=1` paths from
   `banks-audio-route`; document the AHUB endpoint as dead in
   `docs/jetson-audio.md`.
2. Bake the v1 SP filter-chain config (Section B.6) into a new recipe
   `meta-seeed-jetson/recipes-multimedia/banks-audio/files/99-banks-spkprot.conf`
   under the existing `banks-audio` recipe path, gated on the devkit
   machine (since A203 has no I2S DAC). Don't gate it on
   `PCM5102A` specifically — leave the config tunable so it survives the
   first real driver/amp swap.
3. Add CAPS (`caps` recipe → installs `caps.so` to `/usr/lib/ladspa/`) to
   the devkit packagegroup. Pre-flight: `pw-cli list-objects | grep
   Compress` after a PipeWire restart; if filter-chain hosts it, we're
   good.
4. Pick **placeholder tuning** based on the PCM5102A → typical small
   bench-test speaker case (Fs ≈ 80–120 Hz, thermal limit irrelevant
   below ~3 W): HPF at 100 Hz LR2, Compress threshold –6 dBFS, ratio 8:1,
   attack 1 ms, release 100 ms, clamp ±0.95.
5. Wire a Settings → Audio toggle in banks-frontend to enable/bypass the
   SP sink. Useful for A/B-ing the limiter under real program material.

### What to defer to v2

1. LV2 host in PipeWire (`-Dlv2=enabled` + lilv + lv2 recipes).
2. lsp-plugins-lv2 recipe (`sc_limiter`, `mb_limiter`).
3. Per-driver thermal & excursion model — meaningless until drivers + amp
   are selected (Part C unknowns).
4. Cabin FIR room-correction via the existing builtin `convolver` node.
5. Multi-zone / per-channel SP if/when the head-unit drives more than
   front-pair.

### Not doing

1. **Custom ADSP plugin for SPKPROT.** Per
   `docs/nvadsp-custom-plugin-feasibility.md` history, the toolchain to
   build `nvspkprot.elf` equivalents is partner-only. Even if we got it,
   the ALSA control surface (`set params / send bytes`) is hostile to
   tunability. Hard pass.
2. **Patching `tegra210_ahub.c` to expose a fake silicon driver for
   `nvidia,tegra210-spkprot`.** The register window (`0x2908c00`–
   `0x2908fff`, 1 KB) is undocumented; there is no public TRM section.
   We can't blind-poke registers and expect predictable behaviour.

---

## Appendix — quick reference

| Question | Answer |
|---|---|
| Does SPKPROT1 do silicon DSP? | No. Routing endpoint only, intended consumer is ADSP `nvspkprot.elf`. |
| Does any kernel driver bind `nvidia,tegra210-spkprot`? | No. Verified across `sound/soc/tegra/`, `sound/soc/tegra-alt/`, `nvidia/sound/soc/tegra-alt/`, `nvidia/sound/soc/tegra-virt-alt/`. |
| Does the TRM document SPKPROT registers? | No. Address-map and AHUB mux entries only. |
| Does PW 1.0.9 ship a builtin compressor/limiter/dcblock? | No. Static IIRs, math, convolver, delay, clamp. (CLAUDE.md note about `dcblock` was wrong.) |
| Can PW 1.0.9 host LADSPA? | Yes, always built. |
| Can PW 1.0.9 host LV2? | No — `-Dlv2=disabled` in our PACKAGECONFIG; needs new lilv/lv2 recipes. |
| Is CAPS LADSPA in our layers? | Yes, `meta-multimedia/recipes-multimedia/caps/caps_0.9.26.bb`. |
| Best v1 SP block? | `bq_highpass` (DC) + `bq_highpass` (sub-Fs) + CAPS `Compress` + `clamp` per-channel, behind the existing `banks_eq` PEQ sink. |
| Best v2 SP block? | lsp-plugins-lv2 `sc_limiter` / `mb_limiter` once LV2 host is enabled in PW. |
| CPU cost of v1? | ~6% of one Carmel core for stereo; ~6× that for 6-ch surround. Fine. |
