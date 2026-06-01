# Tegra194 AHUB / Audio EQ Stack — Deep Dive

## 1. Where AHUB lives in the SoC

The audio subsystem on Xavier NX is the **APE** (Audio Processing Engine) — a self-contained island with its own clock domain, DMA, IRAM, and a dedicated Cortex-A9 DSP. Inside APE sits the **AHUB** (Audio Hub), a crossbar-routed fabric of audio accelerators plus all physical-layer I/O blocks (I2S, DMIC, DSPK, SPDIF). Software talks to AHUB exclusively through the ADMAIF DMA gateway.

```text
                     APE (Audio Processing Engine)
 +-----------------------------------------------------------------+
 |  Cortex-A9 ADSP <--> ARAM (internal SRAM) <--> ADMA (DMA engine)|
 |                                                       |         |
 |                                                       v         |
 |   AHUB crossbar <--> ADMAIF (SW<->fabric gateway, 20 ch each)   |
 |       ^                                                         |
 |       +-- I2S0..5, DSPK0/1, DMIC0..3, SPDIF (physical pins)     |
 |       +-- AMX0..2 / ADX0..2 (mux / demux)                       |
 |       +-- SFC0..3 (sample-rate conv)                            |
 |       +-- ASRC (high-quality async SRC) + ARAD (ratio detect)   |
 |       +-- AFC0..5 (clock-domain bridge between I2S blocks)      |
 |       +-- MVC0/1 (HW-ramped master / per-stream volume)         |
 |       +-- MIXER0 (10-input x 5-output, per-input gain)          |
 |       +-- OPE0/1 = PEQ + MBDRC (the EQ + multiband DRC block)   |
 +-----------------------------------------------------------------+
```

Outside APE: HDA (HDMI audio path) is separate — doesn't go through AHUB. That's why HDMI audio works on a board with all AHUB drivers disabled (A203 case).

## 2. AHUB crossbar — the dataflow fabric

AHUB is a time-multiplexed digital audio crossbar running at 49.152 MHz (the audio reference clock). Every accelerator and I/O block exposes one or more input + output ports to the bar. Any output can be routed to any input (with mux selectors). The kernel exposes the muxes as ALSA controls — `amixer -c APE` shows them.

A typical playback path on devkit:

```text
PipeWire ALSA out --> ADMA channel 1 --> ADMAIF1 TX
                                           | (xbar: ADMAIF1 -> MVC1)
                                           v
                                         MVC1 (volume + mute, HW-ramped)
                                           | (xbar: MVC1 -> OPE0)
                                           v
                                         OPE0: PEQ -> MBDRC (or reverse)
                                           | (xbar: OPE0 -> I2S5)
                                           v
                                         I2S5 BE (DAP5 pads -> PCM5102A pins)
```

Each `→` is one bar slot. Routing is reconfigured via `amixer cset name='I2S5 Mux' 'ADMAIF1'` etc. Today we're running the simplest route (ADMAIF1 → I2S5); turning on the chain in the middle costs nothing extra at this clock.

## 3. Frontends (FE) vs Backends (BE) — the ALSA mental model

ASoC (the kernel ALSA layer for SoC audio) splits the world:

- **FE (Front End)** — the side userspace talks to. On Tegra194 these are the ADMAIF channels (20 TX + 20 RX). Each FE is a `pcmC1D0p`, `pcmC1D1p`, etc. — what `aplay -l` and PipeWire's monitor see.
- **BE (Back End)** — the physical block (I2S5, DMIC2, SPDIF). Userspace never opens a BE directly. Routing FE↔BE is dynamic — that's the **DPCM** model.
- Everything between FE and BE is an accelerator with its own enable + mux. **DAPM** (Dynamic Audio Power Management) walks the route graph and powers on only the blocks in active paths.

That's why on A203 you see `Broken Path1 - FE not linked to BE` spam: the machine driver enumerates all 20 ADMAIFs against the DT-described BE list, finds no I2S codec, and complains. Cosmetic on devkit (which has I2S5_DUMMY codec wired up), but if you don't disable the unused FE/BE pairings the kernel still walks them all.

## 4. ADMAIF — the SW gateway

ADMAIF is 20 TX + 20 RX channels, each pairing one ADMA DMA channel with one AHUB bar port. Each channel has its own configurable buffer size. Userspace pushes PCM frames over PCIe/AXI → ADMA → ADMAIF Tx → bar.

Key facts:

- ADMA channel and ADMAIF channel must pair on the same index. Disable order matters — disable ADMA first, then ADMAIF, otherwise you hang.
- ADMAIF only handles raw PCM. Format conversion (sample-rate, bit-depth, channel count) is done by accelerators in the bar between ADMAIF and BE.
- 20 channels means 20 independent streams in flight at once. PipeWire usually uses 1–2 at a time. Plenty of headroom for per-source-EQ designs.

## 5. Physical I/O blocks (BEs)

| Block | Count | Notes |
|---|---|---|
| I2S | 0..5 (6 total) | Standard I2S/TDM. DAP[N] pads. I2S5 on 40-pin header (DAP5 pins) is the one we're driving PCM5102A from. |
| DMIC | 0..3 (4 total) | PDM digital mic input. Includes built-in decimation, fully programmable. |
| DSPK | 0/1 (2 total) | PDM speaker output (1-bit Σ-Δ) — for low-power class-D amps. |
| SPDIF | 1 | Optical/coax digital output. Also dummy codec for graph-completion. |
| HDMI audio | via HDA, outside AHUB | Card 0 in ALSA. Separate world from card 1 (APE). |

## 6. AHUB accelerators (what's actually in the bar)

### AMX / ADX — multiplex / demultiplex (3 each)

- **AMX**: combines up to 4 input streams × 16 channels each into one output frame — programmable per-byte mapping. Use case: mix BT + media + nav prompts into a single I2S TX. Has "Wait for all" mode where the output frame only emits when all active inputs have data.
- **ADX**: opposite direction. Split a TDM input into up to 4 streams.

### SFC — Sampling Frequency Converter (4 total)

Synchronous sample-rate conversion. Supported rates (table 7.109 in TRM): 8 / 11.025 / 16 / 22.05 / 24 / 32 / 44.1 / 48 / 88.2 / 96 kHz, bidirectional. 176.4 / 192 only as input (downsampling only). Fixed-ratio integer/rational conversion — cheap, no PLL involvement.

### ASRC + ARAD — Arbitrary Sample-Rate Conversion

ASRC is the high-quality async SRC for two clock domains that drift relative to each other (e.g. BT module clock vs SoC). Feeds clock-ratio info from ARAD (Audio Ratio Detector — measures the ratio between two reference clocks, smooths out one-off jitter). Critical for clean BT/cellular audio.

### AFC — Audio Flow Controller (6 total)

Bridges two I2S interfaces running at slightly different rates (up to 100 ppm apart). High-fidelity interpolation/decimation in hardware. Use case: external codec on one I2S running at its own crystal vs SoC-clocked I2S. Can also be inserted upstream of AMX to smooth burst traffic.

### MVC — Master Volume Control (2 total)

Up to 7.1 channels in, same out. Per-stream OR master volume. The killer feature: the gain change is **hardware-ramped** in the digital volume control block — every gain transition is smoothly interpolated, no zipper noise, no clicks. Software just writes the target gain register and walks away. This is exactly the click problem we were going to work around in PipeWire `param_eq` via the 0.25 dB / 30 ms ramp — MVC does it for free in silicon.

### MIXER — single AHUB-wide mixer

10 inputs × 5 outputs, programmable per-input-to-output gain. Lets two streams sum in the bar without burning a PipeWire mixer node.

### OPE — Output Processing Engine (2 instances: OPE0, OPE1) — **THE EQ BLOCK**

Per TRM §7.8.2.2.9: *"scalable number of BiQuad stages to support stereo, 5.1, and 7.1 channels, meeting Ultra-Low Power (ULP) audio requirements."* Designed for screen-off / always-on music playback where the main CPU can sleep and ADSP+OPE keep audio flowing at minimum power.

Two sub-blocks chained in series, with software-selectable order (PEQ→MBDRC or MBDRC→PEQ — bit 0 of register at p3677):

#### PEQ — Parametric Equalizer (TRM §7.8.4.15)

- Up to **12 biquad stages per channel** (current arch max — bigger than our 8-band PipeWire plan)
- Multi-channel: stereo, 5.1, or 7.1
- Coefficients loaded via AHUB RAM (`PEQ_AHUBRAMCTL` register: write coefficients via offset addressing, shift, parity-protected)
- Coefficient format Q2.30 by default (configurable shift)
- Live coefficient updates supported

#### MBDRC — Multiband Dynamic Range Compression (TRM §7.8.4.16)

- 4 modes (`MBDRC_MODE`):
  - `0` = BYPASS (passthrough)
  - `1` = FULLBAND — single-band, gain application only, no filtering. Same as a soft-knee compressor.
  - `2` = DUALBAND — 2 bands (LP + HP crossover), per-band sidechain + gain
  - `3` = MULTIBAND — 3 bands (LP + BP + HP), full multi-band compressor
- Crossover structure: `ALL_PASS_TREE` (Butterworth fixed biquads — fastest) or `FLEX` (per-band biquads programmable — matches your speaker)
- Sidechain detection: PEAK or RMS (configurable RMS offset −16 dB to +15.9375 dB in Q5.4)
- Per-channel enable mask (`CHAN_MASK` 8-bit) — apply DRC to some channels, bypass others
- Frame size N1/N2/N8/N16/N32/N64 — controls how fast the detector reacts
- Has its own `MASTER_VOLUME` register feeding back (Q9.23 signed, default 0 dB)

This is a proper broadcast-grade compressor — the kind of thing radio stations use to keep dialog audible against music swings in cabin noise.

## 7. ADSP — the secret weapon (Cortex-A9)

A separate CPU inside APE. Runs `adsp.elf` firmware loaded by the kernel. Can:

- Run audio plugins (EQ, mixing, effects) entirely off the main A57 cores — frees them to sleep
- Decode codecs (AAC, MP3) in hardware-assisted mode
- Implement custom signal processing that AHUB accelerators can't (FFT, convolution, ML-based denoise)
- Be the master of an audio path so the main OS can suspend without dropping playback

In our image `CONFIG_TEGRA_NVADSP=m` is enabled (devkit only — see `audio-soc.cfg`). Today we don't use it — PipeWire runs on the A57 cluster. Long-term it's the path to actually achieve "music keeps playing while everything else suspends," which is part of why the sleep/wake design earlier even matters.

## 8. How userspace actually drives all this

Three layers:

1. **DT bindings** in `tegra194-audio-p3668.dtsi` + `tegra186-audio-dai-links.dtsi` define which BEs exist, what's connected, and the static DAI link table. Currently I2S5 wired to `I2S_DUMMY` codec on devkit.
2. **Kernel ASoC driver** (`tegra210-axbar`, `tegra210-ope`, `tegra210-mvc`, etc., all in `sound/soc/tegra-alt/`) creates the DAPM graph and exposes per-block controls.
3. **ALSA mixer controls** are how userspace pokes everything:
   - `amixer -c APE` lists every control (~500 entries on a fully-enabled APE)
   - Routing: `amixer -c APE cset name='I2S5 Mux' 'ADMAIF1'` selects what feeds I2S5
   - MVC: `amixer -c APE cset name='MVC1 Volume' 80` ramps to ~80% smoothly
   - OPE: `amixer -c APE cset name='OPE1 PEQ Active' on/off`, `OPE1 PEQ Biquad Coeffs <coeff blob>` (TLV — bulk coefficient upload)
   - MBDRC: `amixer -c APE cset name='OPE1 MBDRC Mode' MULTIBAND` etc.

To actually use OPE: insert it in the route (`ADMAIF1 → MVC1 → OPE0 → I2S5`), enable PEQ active, upload coefficients (TLV blob — kernel driver computes biquad coeffs from band freq/Q/gain, you supply that calculation or load from a preset file). DAPM walks the graph, powers OPE, opens the route. Done.

## 9. SW (PipeWire param_eq) vs HW (OPE) — trade-off for our use case

| | PipeWire `param_eq` (SW, A57) | OPE / PEQ + MBDRC (HW, AHUB) |
|---|---|---|
| Bands | 8 (room for more, configurable) | 12 biquads per channel |
| Channels | Stereo (or N parallel chains) | Native stereo / 5.1 / 7.1 |
| Live param writes | Yes via `pw-cli` | Yes via `amixer cset` |
| Click on Gain change | Yes (history-reset in `biquad_set`) — SW ramp required | No — MVC HW ramping; PEQ coefficients can update mid-stream |
| Multi-band compressor | Not built-in | Yes, 3-band + sidechain + RMS detection |
| CPU cost | ~0.5–2 % per band on Carmel @ 48 kHz | 0 % on A57 (lives in AHUB silicon) |
| Power | A57 must stay up | ADSP + AHUB can run with A57 suspended |
| Tooling maturity | Mature, well-documented config syntax | Sparse docs, TLV coefficient upload, mostly used by NVIDIA reference apps |
| Routing complexity | Single virtual sink | Need to wire amixer mux chain + DAPM aware |
| Persistence | Filter-chain `conf.d` drop-in | DT or systemd `amixer cset` script at boot |
| Per-source EQ | Multiple filter-chain instances | One OPE per AHUB, would need to mux/select |

**Verdict for v1**: PipeWire `param_eq` is the right call. Tooling we know, integrates cleanly with the Qt app, agnostic across A203 (no OPE wired) and devkit. Land the userspace EQ first.

**Verdict for v2 / power-savings phase**: OPE is the real prize. The combination of:

- HW-ramped volume (no software ramp loop = no jitter under load)
- 12 biquads per channel (more headroom than we'd ever need)
- 3-band sidechain compressor (cabin-noise compensation: keep voice prompts audible while music plays)
- Runs while A57 sleeps (suspend-while-playing path)

is essentially a "do everything we ever wanted, for free, while burning <100 mW" path. Worth a dedicated phase once the app-side audio UX is locked in. Probably involves writing a small userspace daemon that talks to `amixer` + ADSP firmware loader, and a kernel-side check that the OPE driver in our tree (`CONFIG_SND_SOC_TEGRA210_OPE=m`) actually populates the mixer controls.

## 10. Immediate-future implications

1. **MVC is the click solution.** When wiring `AudioMixer` QObject for master volume, drive MVC1 directly via `amixer -c APE cset name='MVC1 Volume' N` instead of `wpctl set-volume`. Free hardware ramp. (Caveat: works only when sink is the I2S route — fall back to `wpctl` for HDMI / USB / BT paths.)
2. **OPE PEQ can host the 8-band EQ** that PipeWire would otherwise run in software, with 50% more biquads available, when the audio route is APE-bound. App could query "which sink is default" and pick SW vs HW EQ path.
3. **MBDRC is essentially free cabin-noise compensation.** Calibrate once per vehicle, drop into `/etc/banks-audio/` as an `amixer cset` script applied on boot.
4. **ADSP path opens screen-off-suspend-music.** Big UX win for in-cab audio that survives the main OS going low-power. Distinct future-phase milestone.
