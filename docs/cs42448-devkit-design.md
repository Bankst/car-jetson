# CS42448 Audio Codec Devkit — Design Handoff

**Status:** Design intent / wiring spec for handoff into larger project.
**Date:** 2026-06-03
**Scope:** Devkit-style breakout for the Cirrus Logic CS42448 6-in / 8-out audio codec, interfaced via 0.1" headers, with 3.5 mm TRS audio jacks. Operates in **TDM mode** as a pure clock slave. Control over **I²C** (driven by the in-tree `cs42xx8` ASoC driver on the Jetson). Jetson 40-pin cable map in §3.4.

---

## 1. Part Summary — CS42448

- **Function:** 6 ADC inputs / 8 DAC outputs, multibit delta-sigma codec.
- **Package:** 64-pin LQFP only (`CS42448-CQZ` commercial −10/+70 °C, `CS42448-DQZ` automotive −40/+105 °C).
- **Performance (18–24-bit data, A-weighted):**
  - DAC dynamic range: **108 dB differential / 105 dB single-ended**; THD+N −98 dB diff / −95 dB SE.
  - ADC dynamic range: 105 dB diff / 102 dB SE.
  - 16-bit data drops DAC DR to 99 dB → feed ≥18-bit.
- **Max sample rate:** 192 kHz.
- **Control:** I²C or SPI (software mode only — no hardware standalone mode; MUST be configured by a host/MCU or it stays at reset defaults = silent).
- **Datasheet:** DS648F5 — https://statics.cirrus.com/pubs/proDatasheet/CS42448_F5.pdf

### Why CS42448
Selected as a TDM-capable multichannel DAC alternative to TI parts (PCM1690/PCM1681 etc.). Requirement was **TDM mandatory**; CS42448 supports 8-ch TDM playback on a single data line and is an automotive/AVR-class codec. (This is the part being received from the vendor.)

---

## 2. Operating Mode — TDM (slave)

**TDM is the locked operating mode.** Key consequences (datasheet §4.5.6, Table 8, Table 9):

- **Codec is slave-only in TDM.** It cannot source BCLK or LRCK. The host drives MCLK, BCLK, and LRCK for both playback and capture.
- **SCLK (BCLK) = 256 × Fs, fixed.** LRCK = Fs, marks frame start.
- **All 8 DAC channels ride one data line: DAC_SDIN1** (slots 0–7 = AOUT1–8). DAC_SDIN2/3/4 are **unused** in TDM.
- **All 6 ADC channels ride one data line: ADC_SDOUT1** (+2 optional from AUX_SDIN). ADC_SDOUT2/3 **unused** in TDM.
- Time slots 32 bits wide; valid data 16/18/20/24-bit, left-justified in slot; MSB on 2nd rising SCLK after LRCK.
- **At Double-/Quad-Speed (96 k / 192 k) TDM the ADC capture timing is NOT guaranteed** (datasheet note). DAC playback is fine. Treat ≥96 k TDM as **DAC-trustworthy, ADC best-effort**.

DAC and ADC ports share one clock pair on this board: host BCLK/LRCK are bridged to both DAC_SCLK/ADC_SCLK and DAC_LRCK/ADC_LRCK. One SAI port, simultaneous TX+RX TDM frame.

---

## 3. Connectors

### 3.1 I²S Header (host audio interface — used subset)

The host header is physically wired for multi-line I²S (multiple per-port clocks + several 2-ch data lines). **For TDM only a small subset is used; all other pins are NC to the codec.**

| Hdr pin | Header name | → Codec pin | Role |
|--------:|-------------|-------------|------|
| 1 | +5V | VA (via ferrite) + 3V3 LDO input | **Sole board 5 V supply** |
| 2 | GND | GND | ground (also MCLK flank) |
| 4 | MCLK_OUT | MCLK (pin 10), via 33 Ω series | **Host-sourced master clock** |
| 5 | LRCK_O0 | DAC_LRCK (19) + ADC_LRCK (5), bridged | Fs frame start |
| 6 | BCLK_O0 | DAC_SCLK (18) + ADC_SCLK (9), bridged | 256 × Fs bit clock |
| 7 | SD_OUT0 | DAC_SDIN1 (17) | 8-ch TDM playback data |
| 16 | SD_IN0 | ADC_SDOUT1 (13) | 6-ch (+2 aux) TDM capture data |
| 26 | 3V3 | VLS/VLC level reference | host logic level (3.3 V) |
| 27 | GND | GND | ground |

**All other I²S-header pins → NC to codec:** LRCK_O1/O2, BCLK_O1/O2, BCLK_IN0..3, LRCK_IN1..3, SD_O1, SD_O3, SD_IN1/2/3, plus header NC pins.

> The original host mappings to SDIN2/SDIN3 and to codec-sourced BCLKO/LRCKO are **invalid in TDM** (SDIN2/3 unused; codec cannot output clocks as a slave) and are intentionally dropped.

### 3.2 CN3 — Control Header (10-pin, I²C)

Control port. **I²C selected** — mandated by the Jetson software stack: the mainline `cs42xx8` ASoC driver provides only an I²C bus binding (`cs42xx8-i2c.c`, `CONFIG_SND_SOC_CS42XX8_I2C`, compatible `cirrus,cs42448`). There is **no SPI glue** in the kernel driver, so SPI would require writing/maintaining an out-of-tree regmap-SPI shim. I²C is the supported path.

Mode select is by strap, not by host: **CS (pin 1) → GND selects I²C mode.** Address set by **AD0/AD1 straps → GND → I²C address 0x48** (both low). These pins were CS/CDIN in the abandoned SPI map; in I²C they are on-board address straps, not host-driven.

| CN3 pin | Name | → Codec pin | Role |
|--------:|------|-------------|------|
| 1 | SCL2_M | SCL/CCLK (63) | I²C clock |
| 2 | NC | — | — |
| 3 | SDA2_M | SDA/CDOUT (64) | I²C data |
| 4 | VBUS | **NC** | USB unused — do not connect |
| 5 | MISO | NC | SPI-only (unused in I²C) |
| 6 | EX_RST | RST# (3) | active-low reset |
| 7 | SCLK | NC | SPI-only (unused in I²C) |
| 8 | MOSI | NC | SPI-only — AD1 now on-board strap → GND |
| 9 | SS | NC | SPI-only — AD0/CS now on-board strap → GND |
| 10 | GND | GND | common ground |

- **2 kΩ pull-ups on SCL + SDA to VLC (on-board 3V3).** Note: the Tegra gen2 I²C bus may carry on-module pull-ups; if present, omit the board pull-ups to avoid double-loading.
- On-board straps required: **CS/pin1 → GND** (I²C mode), **AD0 → GND**, **AD1 → GND** (address 0x48).
- No INT line on CN3 → codec INT (pin 61) left open. The cs42xx8 driver uses no IRQ, so this is correct — no status polling needed (driver-managed).
- Confirm EX_RST is 3.3 V, active-low, push-pull. If open-drain, add pull-up to VLC.
- SPI fallback (NOT used — would need out-of-tree driver): MISO→64, SCLK→63, MOSI→2, SS→1.

### 3.3 Audio Jacks — 3.5 mm TRS, line level

Outputs/inputs used single-ended (one leg of each differential pair). **Line level only — not headphone drive.** SE full-scale ≈ 0.650 × VA ≈ 3.25 Vpp (1.15 Vrms) at VA = 5 V.

**Outputs — 4× TRS** (use AOUTx+ leg):

| Jack | Tip | Ring | Sleeve |
|------|-----|------|--------|
| OUT-1 | AOUT1+ | AOUT2+ | AGND |
| OUT-2 | AOUT3+ | AOUT4+ | AGND |
| OUT-3 | AOUT5+ | AOUT6+ | AGND |
| OUT-4 | AOUT7+ | AOUT8+ | AGND |

Per leg: `AOUTx+ → DC-block 4.7 µF → 560 Ω series → TRS`, shunt cap **≤100 pF directly on pin** (op-amp stability hard limit), 2.7 nF post-series for LPF, 10 kΩ bleed to AGND. (Passive output filter, datasheet Fig 31.) AOUTx− legs available on unpopulated pads if differential is later wanted.

**Inputs — 3× TRS** (single-ended into AINx+):

| Jack | Tip | Ring | Sleeve |
|------|-----|------|--------|
| IN-1 | AIN1 | AIN2 | AGND |
| IN-2 | AIN3 | AIN4 | AGND |
| IN-3 | AIN5 | AIN6 | AGND |

Per leg: `TRS → DC-block 1 µF → series R → AINx+`; AINx− → VQ reference (SE input filter, Fig 27). Set `ADC1/2/3_SINGLE` bits; AIN5/6 internal MUX = A.

### 3.4 Jetson 40-pin cable (host ↔ board)

Maps the **NVIDIA Xavier NX devkit P3509 40-pin header** (`jetson-xavier-nx-banks-devkit` MACHINE) to this board's I²S header + CN3. Tegra **I2S5** (DAP5) is the audio port and is the **clock master**; the codec is slave-only in TDM. Control is **gen2 I²C**.

**Clocks + TDM data**

| Signal | Jetson 40-pin | Board hdr | → codec |
|---|---|---|---|
| MCLK (AUD_MCLK) | **pin 7** | I²S **pin 4** (MCLK_OUT) | MCLK p10, 33 Ω series |
| BCLK (I2S5) | **pin 12** | I²S **pin 6** (BCLK_O0) | DAC_SCLK18 / ADC_SCLK9 |
| FS / LRCK (I2S5) | **pin 35** | I²S **pin 5** (LRCK_O0) | DAC_LRCK19 / ADC_LRCK5 |
| Playback DOUT→codec | **pin 40** (I2S5 SDOUT) | I²S **pin 7** (SD_OUT0) | DAC_SDIN1 p17 |
| Capture codec→DIN | **pin 38** (I2S5 SDIN) | I²S **pin 16** (SD_IN0) | ADC_SDOUT1 p13 — *deferred* |

**Control (I²C gen2) + reset**

| Signal | Jetson 40-pin | Board hdr | → codec |
|---|---|---|---|
| SDA | **pin 27** | CN3 **pin 3** (SDA2_M) | p64 SDA/CDOUT |
| SCL | **pin 28** | CN3 **pin 1** (SCL2_M) | p63 SCL/CCLK |
| RST# | **pin 29** (soc_gpio41_pq5) | CN3 **pin 6** (EX_RST) | RST# p3 |

**Power + GND**

| Signal | Jetson 40-pin | Board hdr |
|---|---|---|
| +5 V (sole supply) | **pin 2** (or 4) | I²S **pin 1** → VA + LDO in |
| GND | 6 / 9 / 14 / 20 / 25 / 30 / 34 / 39 | I²S **pin 2 + 27**, CN3 **pin 10** |

- **Board 3V3 is the on-board LDO** (VD/VLS/VLC) off the 5 V rail. **Do NOT drive board I²S pin 26 from the Jetson** — leave that wire off. Level match still holds: Jetson 40-pin logic 3.3 V ↔ board VLS/VLC 3.3 V, common GND, no shifter. I²C pull-ups go to the on-board VLC, not Jetson 3V3.
- **MCLK net is the signal-integrity risk.** Keep the Jetson pin 7 → board pin 4 ribbon short; 33 Ω series board-side; flank with Jetson pin 6 GND. The DT `assigned-clock-rate` for AUD_MCLK **must equal the physically-wired rate** (24.576 MHz / 512× primary, 12.288 MHz / 256× fallback if SI is marginal) or `hw_params` fails / codec mis-clocks.
- I2S5 full-duplex needs **both** pin 40 (out) + pin 38 (in) routed. Capture is deferred (playback-first) but wire pin 38 now to avoid a later rework.
- **RST# = soc_gpio41_pq5 = pin 29** — confirm free in the devkit pinmux at the DT stage. Alternates: pin 31/32/33 = soc_gpio42/44/54.
- Unused board nets: DAC_SDIN2-4, AUX_SDIN, ADC_SDOUT2/3 → pull to AGND / leave open per §8.

---

## 4. Power

**Single 5 V input via I²S-header pin 1. USB not used for power or data.**

```
I²S-hdr pin1 +5V ─┬─ ferrite bead + 10µF ──► VA  (5.0 V analog)
                  └─ 3V3 LDO ──┬──► VD   (3.3 V digital core)
                               ├──► VLS  (3.3 V serial-port I/O)
                               └──► VLC  (3.3 V control-port I/O)
GND: I²S-hdr pin2 + pin27, CN3 pin10 → all common (star ground at chip)
```

- **VA = 5 V** (filtered) for best analog performance — do NOT run VA from the 3V3 LDO (3.3 V VA degrades SNR, datasheet Note 1).
- One **low-noise 3V3 LDO** (LDO, not buck) feeds VD + VLS + VLC. 150–300 mA class sufficient.
- 3V3 (VD/VLS/VLC) is **generated on-board by the LDO**, not taken from the host — board I²S pin 26 is **not** wired to the Jetson 3V3. Host logic (Jetson 40-pin) is also 3.3 V, so VLS/VLC match with common GND, **no level translation**.
- **CN3 VBUS (pin 4) = NC.** USB connector, if populated, is mechanical only (D+/D− and VBUS all NC). Single supply source — no diode-OR / no contention.

### Decoupling (datasheet Fig 1)
- VA pins 44, 53: 10 µF + 0.1 µF + 0.01 µF each.
- VD pins 6, 24: 10 µF + 0.1 µF.
- VLS, VLC: 0.1 µF each.
- References (do not omit — pop/noise): VQ 100 µF + 0.1 µF; FILT+_ADC 22 µF + 0.1 µF; FILT+_DAC 4.7 µF + 0.1 µF.

---

## 5. Clocking

**All clocks host-sourced. No on-board oscillator and no provision for one.** MCLK, BCLK, LRCK all arrive on the I²S header; codec is a pure slave.

- **MCLK over 0.1" ribbon** (I²S pin 4) is the main signal-integrity risk (up to ~49 MHz). Mitigations:
  - 33 Ω series termination (source and/or codec end).
  - GND-flanked guard traces on PCB, stitched.
  - Short codec-side stub; via straight to MCLK pin.
  - Header neighbors: pin 2 = GND (one flank). Strap header NC pin 3 to GND for the second flank if controllable; otherwise pin 5 (LRCK) is the slower neighbor and tolerable.
  - No clock buffer/jitter cleaner (no provisions) → host MCLK jitter taken direct. Acceptable for devkit; CS42448 multibit ΔΣ has good jitter tolerance.
- MCLK must be an **integer multiple of Fs and synchronous** with BCLK/LRCK (same host domain — satisfied).
- Set `MFREQ[2:0]` to match the actual host MCLK range.

| Fs | Typical host MCLK | Ratio |
|----|-------------------|-------|
| 44.1 k | 11.2896 / 22.5792 MHz | 256× / 512× |
| 48 k | 12.288 / 24.576 MHz | 256× / 512× |
| 96 k | 24.576 MHz | 256× |
| 192 k | 24.576 / 49.152 MHz | 128× / 256× |

---

## 6. Bring-up (I²C, kernel-driven)

On the Jetson, register setup is **not** hand-written firmware — the in-tree `cs42xx8` ASoC driver does it from Device Tree. Bring-up is therefore: get clocks + DT right, the driver handles register programming over I²C.

1. Bring the 5 V rail up; ensure host MCLK + BCLK + LRCK (Tegra I2S5, master) are running and stable.
2. `EX_RST`/RST# driven by the `reset-gpios` DT property (soc_gpio41_pq5, pin 29) — driver de-asserts on probe.
3. Driver programs over I²C (addr 0x48) from DT/ALSA:
   - MCLK rate from the DT `assigned-clock-rate` on AUD_MCLK → `MFREQ` auto-selected to match MCLK/Fs ratio.
   - format `dsp_a` + `bitclock-master`/`frame-master` on the I2S5 link → **TDM**, codec slave.
   - speed mode from the runtime Fs (48 k → Single-Speed).
   - `ADC1/2/3 Single Ended Mode Switch` kcontrol → SE inputs (set via `amixer`).
4. Per-channel volumes / mutes via ALSA kcontrols (`DAC1-4 Playback Volume`, etc.).
5. No status polling — driver uses no IRQ; INT (codec p61) is NC by design.

---

## 7. Board I/O Summary

| Connector | Pins used | Carries |
|-----------|-----------|---------|
| I²S header | 1, 2, 4, 5, 6, 7, 16, 27 | 5 V, GND, host MCLK/LRCK/BCLK, TDM play (SD_OUT0) + capture (SD_IN0) |
| CN3 (control) | 1, 3, 6, 10 | I²C (SCL/SDA) + RST# + GND |
| 3.5 mm × 7 | — | 3 stereo inputs, 4 stereo outputs (line level, SE) |

- Single 5 V input (I²S pin 1); on-board 3V3 LDO (pin 26 NOT host-driven); no on-board clock; codec configured over **I²C** (CN3, addr 0x48); pure TDM slave.
- Jetson side: I2S5 on the P3509 40-pin header (master), gen2 I²C, RST# on soc_gpio41 (pin 29) — see §3.4.

---

## 8. Open Items / Confirm Before Layout

1. **Host MCLK frequency + Fs** — actual value on I²S pin 4. Sets `MFREQ`, the 256/512× ratio, and bounds ribbon-jitter risk. **Primary open question.**
2. **I²C mode straps** — confirm CS→GND mode-select + AD0/AD1→GND address straps (0x48) are present on-board (datasheet §4.7). SPI map abandoned (no kernel SPI driver).
3. **EX_RST drive type/level** — confirm 3.3 V active-low push-pull; add VLC pull-up if open-drain.
4. **Output/input RC filter component values** — worked cutoff math still to be finalized (currently nominal 560 Ω / 2.7 nF out; input R TBD).
5. **Decoupling BOM** — to be expanded to full reference-designator list.
6. **Header NC pin 3 → GND strap** for MCLK flanking — confirm header pad is controllable.
7. **AUX_SDIN / DAC_SDIN2-4 / ADC_SDOUT2-3** — pull unused inputs (DAC_SDIN2-4, AUX_SDIN) to AGND; leave unused outputs open.

---

## 9. Datasheet References

- CS42448 datasheet DS648F5: https://statics.cirrus.com/pubs/proDatasheet/CS42448_F5.pdf
  - §4.4 System Clocking; §4.5.6 TDM; Table 8 TDM clock ratios; Table 9 I/O channel allocation.
  - §4.7 Control port (SPI/I²C); §4.9 power-up sequence.
  - §7 External filters (input Fig 27, output Fig 31); analog output characteristics p.14.
