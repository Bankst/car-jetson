# Tegra194 AHUB / OPE / ADSP probe — devkit findings

Date: 2026-06-01. Target: `jetson-xavier-nx-banks-devkit`, image built from current `main` (commit 485b0dc-era). Kernel cfg includes `audio-soc.cfg`.

## TL;DR

**Everything we want is already exposed.** No DT rewrite, no driver patch, no firmware build required to bring up MVC + OPE PEQ + MBDRC + AHUB MIXER + ADSP-hosted plugins. Work reduces to:

1. Set `amixer` mux routing to insert MVC1 + OPE1 between `ADMAIF1` and `I2S5`.
2. Pick PEQ biquad stage count, upload coefficients per channel.
3. Optionally enable MBDRC modes.
4. ~~Optionally route through `SPKPROT1`~~ — **dead on R35.6.4**. No kernel driver binds the AHUB-side `nvidia,tegra210-spkprot` compatible, and the ADSP-side plugin is not in the static `adsp-fw.bin`. Routing through it = silent black hole. See `docs/speaker-protection-options.md`.
5. ~~Re-route through `ADSP-FE1/2` for plugin processing~~ — **5 of 6 plugins are dead on R35.6.4**. Only `wire` (passthrough) actually loads. `mp3-dec1` / `aac-dec1` / `aec` / `src` / `spkprot` all return `Failed to init app` because dynamic ELF loading is gated by `nvidia,adsp_os_secload` and only `wire` was statically baked into the shipped `adsp-fw.bin`. See `docs/nvadsp-custom-plugin-feasibility.md` and `.planning/intel/jetson-xnx-trm/finding-adsp-plugins-live.md`.

## Hardware inventory (probed)

### MVC — Master Volume Control

| Instance | Per-channel volume | Master volume | Mute | Per-chan mute mask | Curve type | Bypass | Bit format | Channel count | Routing |
|---|---|---|---|---|---|---|---|---|---|
| **MVC1** | 8 channels (`Channel1..8 Volume`) | `MVC1 Volume` | `MVC1 Mute` | `MVC1 Per Chan Mute Mask` | `MVC1 Curve Type` | `MVC1 Bypass` | `MVC1 Audio Bit Format` | `MVC1 Audio Channels` | `MVC1 Mux` (81 sources) |
| **MVC2** | Same | Same | Same | Same | Same | Same | Same | Same | `MVC2 Mux` (81 sources) |

**Result**: two independent MVC blocks. Use one for master cabin volume, one for zone-2 / second-stream / per-source attenuation. HW-ramped gain changes — solves the `param_eq` click problem without software ramp loops.

### OPE1 — Output Processing Engine (PEQ + MBDRC)

Single instance (`OPE1`). `OPE0` not enumerated — devkit silicon variant or DT-disabled.

#### PEQ (Parametric EQ)

| Control | Per channel? | Notes |
|---|---|---|
| `OPE1 PEQ Active` | global | BOOLEAN, default `off` |
| `OPE1 PEQ Biquad Stages` | global | INTEGER, min 0 max **11** (= up to 12 biquads), default **4** |
| `OPE1 PEQ Channel-N biquad gain params` | yes, **N=0..7** | TLV blob — biquad coefficients |
| `OPE1 PEQ Channel-N biquad shift params` | yes, N=0..7 | Per-stage Q-format shift |

**Capability**: 8 channels × up to 12 biquads = **96 biquad slots**. Per-channel independent EQ curves. Full 7.1 + 1 spare, or 8 mono zones, or 4 stereo zones with independent voicing.

#### MBDRC (Multi-Band Dynamic Range Compressor)

| Control | Type | Values |
|---|---|---|
| `OPE1 MBDRC Mode` | enum | `bypass` / `fullband` / `dualband` / `multiband` |
| `OPE1 MBDRC Filter Structure` | enum | `all-pass-tree` / `flexible` |
| `OPE1 MBDRC Frame Size` | int | N1..N64 (per TRM) |
| `OPE1 MBDRC Peak RMS Mode` | enum | peak / rms |
| `OPE1 MBDRC RMS Offset` | int | Q5.4 |
| `OPE1 MBDRC IIR Stages` | int | |
| `OPE1 MBDRC In Threshold` / `Out Threshold` | int | |
| `OPE1 MBDRC Ratio` | int | |
| `OPE1 MBDRC In/Fast Attack Time Const` | int | |
| `OPE1 MBDRC In/Fast Release Time Const` | int | |
| `OPE1 MBDRC Fast Attack Factor` / `Fast Release Factor` | int | |
| `OPE1 MBDRC Attack Gain` / `Release Gain` / `Fast Release Gain` | int | |
| `OPE1 MBDRC Init Gain` / `Makeup Gain` / `Master Volume` | int | |
| `OPE1 MBDRC Shift Control` | int | |
| `OPE1 MBDRC Low Band Biquad Coeffs` | TLV | per-band programmable crossover (FLEX mode) |
| `OPE1 MBDRC Mid Band Biquad Coeffs` | TLV | same |
| `OPE1 MBDRC High Band Biquad Coeffs` | TLV | same |

#### OPE order toggle

`OPE1 direction peq to mbdrc` — bit toggle. PEQ→MBDRC or MBDRC→PEQ in series.

### AHUB MIXER1

10×4 adder matrix exposed (TRM says 10×5; driver exposes 4 adders × 10 RX gains + a 5th passthrough TX).

| Controls | Count |
|---|---|
| `MIXER1 AdderN RX1..RX10` | N=1..4, each adder has 10 input gains |
| `MIXER1 TX1..TX5` | as Mux source items (downstream selectors) |

**Use case**: sum BT + media + nav prompts in HW, no PipeWire mixer needed. Each adder has per-RX gain.

### AHUB accelerators (drivers loaded, controls present)

| Block | Instances | Notes |
|---|---|---|
| AMX | 4 | Byte-map stream combiner |
| ADX | 4 | Byte-map stream splitter |
| SFC | 4 | Sync sample-rate conv |
| ASRC | 1 (6 TX outputs) | Async SRC + ratio detect |
| ARAD | 1 | Ratio detector |
| AFC | 6 | Audio Flow Controller |
| **SPKPROT1** | 1 | **Speaker Protection — independent AHUB endpoint** |
| IQC | 2 | Inter-Quad Coupler |

### I/O blocks (BEs)

| Block | Count | Notes |
|---|---|---|
| I2S | 6 (I2S1..6) | I2S5 = 40-pin DAP5 → PCM5102A |
| DMIC | 4 | PDM mic input |
| DSPK | 2 | PDM speaker output |
| SPDIF | 1 | Digital output |
| HDA / HDMI | separate card | Card 0, outside AHUB |

### ADSP — Cortex-A9, OS running but plugins mostly dead

```
[7.060710] nvadsp 2993000.adsp: in probe()...
[7.138604] nvadsp 2993000.adsp: ADSP OS firmware already loaded
[7.490866] tegra210-adsp tegra210-adsp: Loaded app wire
[7.490877] tegra210-adsp tegra210-adsp: Loaded app apm
[7.490883] tegra210-adsp tegra210-adsp: Loaded app adma
[7.490889] tegra210-adsp tegra210-adsp: Loaded app adma_tx
```

ADSP OS firmware (`adsp-fw.bin`, encrypted + signed) flashed into eMMC partition `adsp-fw` (not on rootfs). Loaded by BPMP/MB1 before kernel probe. No firmware recipe needed.

> **Correction (post-probe).** The 4 "Loaded app" lines above are framework helpers, **not** the 6 DT-declared plugins. Only `wire` of those 6 actually initialises when its mux is engaged. The other 5 all return `Failed to init app` because dynamic ELF loading is gated by `nvidia,adsp_os_secload` (set in `tegra194-soc-audio.dtsi`) and `app.c:317-321` blocks it; only WIRE was statically baked into the shipped `adsp-fw.bin`. See `docs/nvadsp-custom-plugin-feasibility.md` and `.planning/intel/jetson-xnx-trm/finding-adsp-plugins-live.md`.

#### DT-declared ADSP plugins vs actual liveness

| Plugin | Intended use | Live on R35.6.4? |
|---|---|---|
| `wire` | Pass-through | **YES** — only one that initialises |
| `mp3-dec1` | HW-assisted MP3 decode | **NO** — fails to init |
| `aac-dec1` | HW-assisted AAC decode | **NO** — fails to init |
| `src` | Sample-rate conversion (supplemental to AHUB SFC) | **NO** — fails to init |
| `spkprot` | Speaker protection | **NO** — fails to init; **also** AHUB-side endpoint has no kernel driver |
| `aec` | Acoustic Echo Cancellation | **NO** — fails to init |

Net: ADSP is unavailable for any real DSP work on this image. All DSP (room correction, AEC, speaker protection, codec offload) runs on Carmel A57-class cores via PipeWire / gstreamer / ffmpeg. The AHUB hardware blocks (MVC, OPE PEQ + MBDRC, MIXER, AMX/ADX, SFC, ASRC, AFC) remain fully usable.

#### ADSP frontends

- `ADSP-FE1` → ALSA `card 1 dev 20`
- `ADSP-FE2` → ALSA `card 1 dev 21`
- `ADSP-ADMAIF1 MUX` / `ADSP-ADMAIF2 MUX` — ADSP can also tap ADMAIF streams

### Crossbar Mux item count

All 81 mux selectors have identical item-list (cross-bar fully connected). Sources:

```
None | ADMAIF1..20 | I2S1..6 | SFC1..4 | MIXER1 TX1..5 |
AMX1..4 | ARAD1 | AFC1..6 | OPE1 | SPKPROT1 | MVC1 | MVC2 |
IQC1-1..2, IQC2-1..2 | DMIC1..4 | ADX1..4 TX1..4 | ASRC1 TX1..6
```

Any block's output can feed any other block's input. True full crossbar.

## Cabin-EQ target route

```text
PipeWire (ALSA) -> ADMAIF1 -> MVC1 -> OPE1 -> I2S5 -> PCM5102A
```

Translates to three `amixer cset` writes:

```sh
amixer -c APE cset numid=1301 'ADMAIF1'   # MVC1 Mux  <- ADMAIF1
amixer -c APE cset numid=1299 'MVC1'      # OPE1 Mux  <- MVC1
amixer -c APE cset numid=1278 'OPE1'      # I2S5 Mux  <- OPE1
```

Plus enable the EQ + DRC:

```sh
amixer -c APE cset numid=1135 on          # OPE1 PEQ Active
amixer -c APE cset numid=1136 8           # OPE1 PEQ Biquad Stages = 8
amixer -c APE cset numid=1156 dualband    # OPE1 MBDRC Mode = dualband
```

PEQ coefficients per channel get uploaded via the `Channel-N biquad gain params` TLV. Format TBD by reading `sound/soc/tegra-alt/tegra210_ope_alt.c` — likely 5 × Q2.30 fixed-point words per stage (b0, b1, b2, a1, a2).

## Speaker-protection HW path — DEAD on R35.6.4

```text
ADMAIF1 -> MVC1 -> OPE1 -> [SPKPROT1] -> I2S5      # do NOT route through SPKPROT1
```

The SPKPROT1 AHUB endpoint is a black hole:

- No kernel driver binds `nvidia,tegra210-spkprot` (searched `sound/soc/tegra/`, `tegra-alt/`, `nvidia/sound/soc/tegra-alt/`, `tegra-virt-alt/`).
- TRM has no functional chapter for SPKPROT — only address-map entry. No register documentation, no silicon-side DSP.
- Architectural intent (per `tegra-platforms-audio-dai-links.dtsi`) was for SPKPROT1 to ferry frames to the ADSP-side `nvspkprot.elf` plugin, which is not in the shipped `adsp-fw.bin` (per `finding-adsp-plugins-live.md`).

Speaker protection must run in PipeWire on Carmel. See `docs/speaker-protection-options.md` for the recommended filter-chain (`bq_highpass` × 2 + CAPS LADSPA `Compress` + `clamp`).

## Convolution / room correction — runs in PipeWire

Custom ADSP plugins are infeasible (see `docs/nvadsp-custom-plugin-feasibility.md`). PipeWire's builtin `convolver` filter runs on one Carmel core for stereo / 5.1 / 7.1 room-correction IRs:

| Config | Cost (est.) |
|---|---|
| 8k taps × 2 ch | 3–5 % of one Carmel core |
| 8k taps × 6 ch (5.1) | ~9–15 % of one core |
| 16k taps × 6 ch | ~18–30 % of one core |
| 65k taps × 6 ch (~1.4 s IR) | starts spreading across cores |

8k taps @ 48 kHz = 170 ms IR — adequate for cabin RT60 (~50–80 ms typical). Memory negligible (< 1 MB total). Latency one partition block (5–10 ms).

A complementary cheap path: 12-biquad OPE PEQ per channel can fit a minimum-phase magnitude-only approximation of a measured IR (≤±1 dB up to ~5 kHz with `rePhase`-style biquad fitting). Free in silicon, but loses time-domain phase / room-mode nulling.

## Known cosmetic noise

```
[11.176931] tegra210-adsp: Broken Path1 - FE not linked to BE
[11.177162] tegra210-adsp: ASoC: error at snd_soc_component_open on tegra210-adsp: -32
```

Machine driver enumerates all ADSP FE/BE pairings; some unused ones throw `-32`. Cosmetic. Will likely disappear once we wire the route properly. If not, demote `dev_err → dev_dbg` in `tegra-alt/tegra210_adsp_alt.c:1409` + `:1734`.

## Reference: phased plan

| Phase | Goal | Status |
|---|---|---|
| **P1 — probe** | Inventory HW + ADSP capability | **DONE** (this doc) |
| **P2 — boot-time cset** | Static cabin route at boot via `banks-audio-route` recipe | **DONE** (commits d03e22b, d5cd668) |
| **P3 — MVC + PEQ live tool** | `banks-audio-hw.sh` CLI: live coefficient upload + ramped gain via `amixer` | Next |
| **P4 — MBDRC tuning** | Cabin-noise compressor calibrated, persistent preset | After P3 |
| ~~**P5 — SPKPROT enable**~~ | ~~HW speaker protection~~ | **CUT** — block is dead on R35.6.4; SW path via PipeWire instead (see `speaker-protection-options.md`) |
| ~~**P6 — ADSP `aec` for HFP**~~ | ~~HW AEC for BT calling~~ | **CUT** — plugin dead; use `webrtc-audio-processing` in PipeWire |
| **P5' — SW SP filter-chain** | PipeWire SP chain (`bq_highpass` × 2 + CAPS Compress + `clamp`) | After P4 |
| **P6' — PipeWire upgrade to 1.4.10** | Gets `dcblock` + fftw-backed `convolver` + `ebur128` (see `pipewire-upgrade-feasibility.md`) | Concurrent w/ P5' |
| **P7 — convolver room correction** | Mic-IR convolution in PipeWire (NOT ADSP) | After P6' |
| **P8 — mic calibration tool** | UMIK-1 sweep → IR | After P7 |
| **P9 — Qt integration** | `AudioMixer` (MVC) + `AudioEQ` (PEQ) + `AudioRoom` (convolver) QObjects | After P3 (UI for what's live) |
