# CS42448 TDM (8 out / 6 in) — Migration Plan

Replace the PCM5102A 2-channel pin-strap DAC on the devkit (`jetson-xavier-nx-banks-devkit`, I2S5 / DAP5 on the 40-pin header) with a **CS42448** TDM codec breakout: 8 DAC outputs (4 stereo TRS jacks) + 6 ADC inputs (3 stereo TRS jacks) on a single TDM data pair.

Hardware spec: `docs/cs42448-devkit-design.md` (design handoff, 2026-06-03). This plan is the software/integration side of that board.

## Board facts locked by the handoff

- **Operating mode: TDM, codec is slave-only.** Cannot source BCLK/LRCK. Host (Tegra) drives MCLK + BCLK + LRCK for both directions. → Tegra I2S5 **must be clock master** (`bitclock-master; frame-master;`). Not a choice.
- **One data line each way**: 8 DAC ch on DAC_SDIN1 (slots 0–7), 6 ADC ch on ADC_SDOUT1. Single SAI port, simultaneous TX+RX. Maps cleanly to I2S5 TDM.
- **BCLK = 256 × Fs, fixed.** 32-bit slots × 8 = 256 bits/frame = 256·Fs. At 48 kHz → BCLK 12.288 MHz. ✓
- **MCLK = 256× or 512× Fs**, host-sourced on header pin 4. At 48 kHz: 12.288 or 24.576 MHz. No on-board oscillator. **MCLK over the 0.1″ ribbon is THE critical integration risk** (24.576 MHz, up to ~49 MHz at 192k) — see dedicated MCLK section below.
- **Feed ≥18-bit** (16-bit drops DAC DR to 99 dB). → use 24-bit data in 32-bit slots.
- **Control bus: I²C** (per your call; driver supports it natively). **Control-port mode is set by strapping CS (codec pin 1, AD0/CS) to GND → I²C mode.** With CS=GND, AD0=0; I²C addr = 0x48 + AD1 strap (0x48 / 0x4a). CN3 rewire for I²C: SCL2_M→codec pin 63 (SCL/CCLK), SDA2_M→codec pin 64 (SDA/CDOUT), 2 kΩ pull-ups to 3V3 (VLC). CN3 MOSI/MISO/SCLK/SS unused. → Tegra gen2 I²C (header pins 27/28).
- **Single 5 V supply** (header pin 1), on-board 3V3 LDO feeds VD/VLS/VLC. **VA = 5 V** (don't run from 3V3 — SNR). Host logic = **3.3 V** (VLS/VLC), matches Jetson 40-pin header level → **no level translation**.
- Line-level SE outputs ≈ 1.15 Vrms @ VA=5V. Not headphone drive.
- No INT wired — poll status reg 0x19 for clock-error/overflow (Linux codec driver handles its own bring-up; polling only matters for bare-metal — N/A here).

## Feasibility — confirmed against the live kernel tree

- **Codec driver present, I²C native.** `sound/soc/codecs/cs42xx8.c` (regmap-agnostic core, exports `cs42xx8_probe(dev,regmap)` + `cs42xx8_regmap_config` + `cs42448_data`) + `cs42xx8-i2c.c`. Kconfig `SND_SOC_CS42XX8_I2C` ("Cirrus Logic CS42448/CS42888 CODEC (I2C)"), compatible `cirrus,cs42448`. TDM via `SND_SOC_DAIFMT_DSP_A` → `CS42XX8_INTF_DAC_DIF_TDM | CS42XX8_INTF_ADC_DIF_TDM` (`cs42xx8_set_dai_fmt`). Implements `set_dai_fmt` + `set_dai_sysclk` (MCLK→MFREQ). Binding `Documentation/devicetree/bindings/sound/cs42xx8.txt`: `clocks`/`clock-names="mclk"`, `VA/VD/VLS/VLC-supply`, optional `reset-gpios`.
  - **No SPI glue exists** (`cs42xx8-spi.c` absent, no `SND_SOC_CS42XX8_SPI`) — irrelevant now that control is I²C.
- **I2S5 pins free on stock devkit hdr40**, no conflicts (`.../jakku/kernel-dts/tegra194-p3668-all-p3509-0000-hdr40.dts`):
  - pin 7 `aud_mclk_ps4` (fn `aud`, out) → breakout pin 4 MCLK
  - pin 12 `dap5_sclk_pt5` (i2s5) → breakout pin 6 BCLK
  - pin 35 `dap5_fs_pu0` (i2s5) → breakout pin 5 LRCK
  - pin 40 `dap5_dout_pt6` (i2s5, out) → breakout pin 7 SD_OUT0 (→ codec DAC_SDIN1, playback)
  - pin 38 `dap5_din_pt7` (i2s5, in) ← breakout pin 16 SD_IN0 (← codec ADC_SDOUT1, capture)
- **I²C on header**: `gen2_i2c` pins 27 (SDA) / 28 (SCL). Spare GPIOs for RESET: `soc_gpio41_pq5`, `soc_gpio42_pq6`, `soc_gpio44_pr0`, `soc_gpio54_pn1`.
- **DT wiring pattern proven**: stock I2S5 link `i2s5_to_codec` (`audio/tegra186-audio-dai-links.dtsi:1003`, `codec{sound-dai=<&tegra_i2s5 I2S_DUMMY>}`) + graph loopback `i2s5_dap_ep`↔`i2s5_dummy_ep` (`audio/tegra186-audio-graph.dtsi:1777`). Real-codec template: rt5658 on Galen `tegra194-audio-p2822-0000.dtsi`. TDM template: `overlays/jetson-respeaker-4-mic-lin-array.dts` (`format="dsp_a"; fsync-width=<0>; bitclock-master; frame-master;`).
- **Tegra TDM mechanism**: machine driver `tegra_machine_driver.c:169` calls `snd_soc_dai_set_tdm_slot(cpu_dai, mask, mask, 0, 0)` on DSP_A/DSP_B; I2S driver `tegra210_i2s_alt.c:334` stores masks, writes `total_slots-1` + tx/rx. Generic `dai-tdm-slot-num` NOT read — slots derived from channel count + DSP_A.

## Full wiring map (breakout ↔ Jetson 40-pin)

| Breakout | Signal | Jetson 40-pin |
|---|---|---|
| I²S hdr 1 | +5V | pin 2 or 4 (5V) |
| I²S hdr 2/27, CN3 10 | GND | any GND (6/9/14/20/25/30/34/39) |
| I²S hdr 4 | MCLK | pin 7 (aud_mclk) |
| I²S hdr 5 | LRCK | pin 35 (dap5_fs) |
| I²S hdr 6 | BCLK | pin 12 (dap5_sclk) |
| I²S hdr 7 | SD_OUT0 (play→codec) | pin 40 (dap5_dout) |
| I²S hdr 16 | SD_IN0 (cap←codec) | pin 38 (dap5_din) |
| I²S hdr 26 | 3V3 logic ref | pin 1 or 17 (3.3V) |
| CN3 1 (SCL2_M) | I²C SCL | pin 28 (gen2_i2c_scl) |
| CN3 3 (SDA2_M) | I²C SDA | pin 27 (gen2_i2c_sda) |
| CN3 6 (EX_RST) | RST# | a spare GPIO (e.g. soc_gpio41) |

Board mods vs as-drawn-SPI: strap CS (pin1) to GND for I²C mode; AD1 sets addr LSB; 2 kΩ SDA/SCL pull-ups to 3V3; route SCL→codec 63, SDA→codec 64; CN3 SPI lines unused.

## MCLK — critical wiring (gating risk)

MCLK is the highest-risk net on the whole link: a 24.576 MHz (≥48k @ 512×) clock running over a 0.1″ ribbon into a slave codec with **no on-board buffer or jitter cleaner** (handoff §5 — no provisions). Jitter/reflections here = audible artifacts or codec PLL unlock. Wiring requirements:

- **Source termination**: 33 Ω series at the Tegra (pin 7) end; optionally a second 33 Ω at codec end. Match to ribbon impedance.
- **GND flanking**: MCLK must be GND-flanked the entire run. On the breakout I²S header, pin 2 = GND is one flank; strap a header NC neighbor to GND for the second flank. On any custom ribbon/cable, run MCLK as a twisted pair with GND (or coax) — do NOT route it as a bare ribbon conductor next to other clocks.
- **Keep it short**: minimize total MCLK length; short codec-side stub, via straight to the MCLK pin. Long ribbon = the dominant failure mode.
- **Lower the rate if marginal**: prefer **mclk-fs = 256 (12.288 MHz)** over 512 if SI proves marginal — halves the clock frequency at the cost of MCLK=BCLK (still a valid CS42448 ratio at 48k). 512× only if 24.576 MHz proves clean.
- **Separation**: keep MCLK away from the analog output traces and the DC-blocked jack lines to avoid coupling 24 MHz into line outputs.
- MCLK must stay integer-multiple and synchronous with BCLK/LRCK (same Tegra PLLA domain — satisfied by sourcing all three from AUD_MCLK/I2S5).
- Set `MFREQ[2:0]` to the actual MCLK range — the cs42xx8 driver does this from the `mclk-fs` × Fs product via `set_dai_sysclk`; **the DT `mclk-fs` value must match the physically wired MCLK ratio** or the codec mis-clocks.

If MCLK SI can't be made reliable over the chosen cable, the fallback is an on-codec-board oscillator + clock buffer (board respin — not in current design).

---

## Implementation phases

### HW1 — board / wiring (user)
- **Strap CS (pin1) → GND = I²C mode.** AD0 + AD1 both low → **I²C addr 0x48**. SCL→codec 63, SDA→codec 64, 2 kΩ pull-ups to 3V3.
- **MCLK wiring per the dedicated MCLK section** — 33 Ω, GND-flank/shield, short. The make-or-break net. (24.576 MHz, 512×@48k.)
- **RST# wired** to a chosen spare GPIO (soc_gpio41/42/44/54) → DT `reset-gpios`.
- Confirm 40-pin DAP5 + gen2-I²C header level = 3.3V (Jetson 40-pin spec — expected yes).

### SW1 — kernel config fragment
- `recipes-kernel/linux/linux-tegra/cs42448.cfg`:
  ```
  CONFIG_SND_SOC_CS42XX8=m
  CONFIG_SND_SOC_CS42XX8_I2C=m
  ```
- Devkit-gated in `linux-tegra_%.bbappend`: `SRC_URI:append:jetson-xavier-nx-banks-devkit = " file://cs42448.cfg"`

### SW2 — device tree (bulk of work)
Devkit DT include adding:
1. **Codec node** on gen2 I²C controller (find phandle, ~`&gen2_i2c` / `i2c@c240000`):
   ```dts
   cs42448: codec@48 {
       compatible = "cirrus,cs42448";
       reg = <0x48>;
       clocks = <&bpmp TEGRA194_CLK_AUD_MCLK>;
       clock-names = "mclk";
       VA-supply = <&reg_cs_va>;  VD-supply  = <&reg_cs_vd>;
       VLS-supply = <&reg_cs_vls>; VLC-supply = <&reg_cs_vlc>;
       reset-gpios = <&tegra_main_gpio TEGRA194_MAIN_GPIO(Q,5) GPIO_ACTIVE_LOW>;
       #sound-dai-cells = <1>;
       status = "okay";
       port { cs42448_ep: endpoint {
           remote-endpoint = <&i2s5_dap_ep>;
           mclk-fs = <512>;            /* 24.576 MHz @ 48k; 256 also valid */
           link-name = "cs42448-tdm";
       };};
   };
   ```
2. **DAI-link override** (`&i2s5_to_codec`): `format="dsp_a"; bitclock-master; frame-master; fsync-width=<0>; codec{ sound-dai=<&cs42448 0>; };`
3. **Graph endpoint** (`&i2s5_dap_ep { remote-endpoint=<&cs42448_ep>; }`).
4. **Regulators**: fixed-regulators for VA/VD/VLS/VLC (or reuse rails). The codec is externally powered (board LDO) — these are nominal `regulator-fixed` always-on stubs for the driver's `*-supply` requirement.
5. **Delivery**: stock devkit DTB is `tegra194-p3668-all-p3509-0000.dtb`, no custom recipe yet. Recommend new `seeed-devkit-devicetree` recipe (mirror A203's `seeed-a203-devicetree`): DTS `#include`s stock + `cs42448.dtsi`, set via `UBOOT_EXTLINUX_FDT`.

### SW3 — AHUB route update (`banks-audio-route`)
- I2S5: channels 2→**8**, bits→**32** (24 valid), format dsp_a/TDM.
- OPE1 PEQ already 12-stage × **8-channel** — keep. Verify MVC1 multichannel; if stereo-only, route ADMAIF1→OPE1→I2S5 (drop MVC) or per-channel MVC.
- `route.conf` defaults: `I2S5_CHANNELS=8`, `I2S5_BITS=32`.
- **Capture leg — OPTIONAL / deferred** (per user: 6 ADC in likely ignored). If/when wanted: I2S5 (6ch) → ADMAIF → `arecord -c 6 -f S24_LE -D hw:APE`. Focus everything else on the 8ch playback path.
- **AUX_SDIN = NOT usable / irrelevant.** It's a capture-domain *input* (2 external digital ch multiplexed into the ADC TDM frame), adds zero DAC/output channels. Linux `cs42xx8` driver caps capture at `num_adcs*2 = 6` (`cs42xx8.c:608`) with no AUX handling — slots 7–8 never reach host without a driver patch, and we have no digital source to feed it. Pull AUX_SDIN to AGND (handoff §8.7); AUX_LRCK/BCLK header pins = NC (secondary serial-port clocks, unused in single-port TDM).

### SW4 — PipeWire / WirePlumber + multi-mode DSP
**Full design: `docs/jetson-audio-multimode-dsp.md`** (modes 2.0/2.1/4.1/5.1/7.1, max-HW DSP split).
- Permanent 8-ch HW sink (`banks_cabin_hw`) bound to CS42448/I2S5; per-mode `channelmix` upmix front-ends; stable logical sink for apps.
- PipeWire does only: decode/resample, matrix upmix, per-channel delay. **All EQ/crossover/volume/limiter/per-speaker-trim in AHUB** (MVC + OPE PEQ + MBDRC).
- `banks-audio-mode <mode>` manager applies per-mode OPE PEQ banks + MBDRC + MVC + PW matrix atomically; route is mode-invariant.
- Retire stereo `param_eq` (`99-banks-eq.conf`) for cabin path — EQ → HW OPE 8ch.

### SW5 — `banks-audio-hw.py`
- PEQ/MVC ctypes path unchanged (silicon 8ch). PEQ writer must loop up to 8 channel coeff sets. DAPM keepalive must open an 8ch substream. Add capture-monitor for the 6 ADC ins.

---

## Locked decisions
- **Sample rate = 48 kHz.** Source is BT A2DP (44.1/48k native); 96/192k give no audible benefit in-cabin, cost 2–4× DSP, worse MCLK SI, and ADC TDM unreliable ≥96k. 48k is transparent + max DSP headroom. (Rationale: sample-rate analysis, this is the right rate; spend effort on EQ/crossover/placement.)
- **MCLK = 24.576 MHz (512× @ 48k, MFREQ=4)** primary; fall back to 12.288 MHz (256×) if 24.576 SI proves marginal over the ribbon. Both well within PLLA + AUD_MCLK pad limits. DT `assigned-clock-rates` for `TEGRA194_CLK_AUD_MCLK` must equal the wired value.
- Control = I²C (CS→GND); codec slave → Tegra master (mandatory); DTB delivery = new devkit recipe.
- **Board = bare CS42448** (no on-board DSP). The `docs/audio-board/` ADAU1452 SigmaDSP files belong to a *separate* devkit variant — ignore for this integration; only the CS42448 datasheet/schematic/PCB PDFs there apply. All DSP runs on the Jetson AHUB (this plan), not an external DSP.
- **I²C address = 0x48** — AD0 and AD1 both strapped low (base addr, AD0=AD1=0).
- **RESET wired** — `reset-gpios` populated (driver drives it on probe/PM); **`soc_gpio41_pq5` = `TEGRA194_MAIN_GPIO(Q,5)` = 40-pin header pin 29** (confirmed free, GPIO-muxed; adjacent GND pin 30 + I²C pins 27/28 → tidy RST#+I²C routing). Alts: pin 31/32/33 (soc_gpio42/44/54).
- **PipeWire = single fixed 8ch sink + live 64-coeff matrix** (runtime-switchable, glitchless); **all DSP in AHUB** (MVC/OPE/MBDRC), PW does zero-fill + matrix + delay only. Full design: `jetson-audio-multimode-dsp.md`.
- **Resample = PipeWire** (per-stream); SFC unused.

## Open decisions (need user)
1. **RESET GPIO** — confirm `soc_gpio41` free at pinmux (vs 42/44/54).
2. **Host MC player** packaging — mpv (+ gstreamer via Qt); decide at multichannel-test phase.
3. **Commissioning tuning** (non-blocking): crossover freq, upmix flavor, LFE-fold, per-channel delay — see multimode doc defaults.

## Risks / gotchas
- **Single-ended ADC config** (capture only — deferred): board wires inputs SE. Driver **exposes** `ADC1/2/3 Single Ended Mode Switch` kcontrols (`CS42XX8_ADCCTL` bits 4/3/2) → set via `amixer`, **no driver patch needed** (earlier risk retracted). Moot while capture ignored.
- **set_dai_sysclk fires via `mclk-fs`** in the graph endpoint (framework → codec MFREQ). rt5658 uses same path → expected OK; verify.
- **INT line not needed**: `cs42xx8` driver has zero IRQ usage, binding has no `interrupts` prop. INT (codec pin 61) = NC is correct; no interrupt GPIO in DT. (INT/reg-0x19 polling is bare-metal-only; Linux ignores it.)
- **8ch through OPE**: PEQ coeff blob is per-channel (62×s32 × up to 8ch) — `banks-audio-hw.py` must loop channels.
- **DAPM keepalive** must open 8ch substream once TDM is 8-slot.
- **MCLK SI**: 24.576 MHz over 0.1″ ribbon — 33 Ω + GND flank per handoff §5.
- Reference: `docs/cs42448-devkit-design.md`, `docs/jetson-audio.md`, `docs/jetson-audio-hw-probe.md`, memory `project_audio_hw_eq.md`.
