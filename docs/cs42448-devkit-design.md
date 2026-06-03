# CS42448 Audio Codec Devkit — Design Handoff

**Status:** Design intent / wiring spec for handoff into larger project.
**Date:** 2026-06-03
**Scope:** Devkit-style breakout for the Cirrus Logic CS42448 6-in / 8-out audio codec, interfaced via 0.1" headers, with 3.5 mm TRS audio jacks. Operates in **TDM mode** as a pure clock slave.

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

### 3.2 CN3 — Control Header (10-pin, SPI)

Control port. **SPI selected** (clean 1:1 map; I²C mode would strand AD0/AD1 address straps not present on this header). Brings signal + GND only — **no power** (VBUS unused).

| CN3 pin | Name | → Codec pin | Role |
|--------:|------|-------------|------|
| 1 | SCL2_M | NC | I²C alt (unused in SPI) |
| 2 | NC | — | — |
| 3 | SDA2_M | NC | I²C alt (unused in SPI) |
| 4 | VBUS | **NC** | USB unused — do not connect |
| 5 | MISO | SDA/CDOUT (64) | SPI data out (codec→host) |
| 6 | EX_RST | RST# (3) | active-low reset |
| 7 | SCLK | SCL/CCLK (63) | SPI clock |
| 8 | MOSI | AD1/CDIN (2) | SPI data in (host→codec) |
| 9 | SS | AD0/CS (1) | chip select (active low) |
| 10 | GND | GND | common ground |

- No INT line on CN3 → codec INT (pin 61) left open; **poll status register 0x19** in firmware for clock-error/overflow.
- Confirm EX_RST is 3.3 V, active-low, push-pull. If open-drain, add pull-up to VLC.
- I²C fallback (if ever needed): SCL2_M→pin63, SDA2_M→pin64, 2 kΩ pull-ups to VLC, and add on-board AD0/AD1 address straps.

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
- Host logic = 3.3 V (header pin 26) → VLS/VLC match, **no level translation**.
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

## 6. Firmware Bring-up (SPI)

1. Bring rails up; ensure host MCLK + BCLK + LRCK are running and stable.
2. Release `EX_RST` (RST#) high.
3. SPI register writes:
   - `MFREQ[2:0]` = host MCLK range.
   - `DAC_FM` / `ADC_FM` = speed mode (Quad-Speed for 192 k — DAC trustworthy, ADC not guaranteed at QSM TDM).
   - `DAC_DIF` / `ADC_DIF` = **TDM**.
   - `ADC1/2/3_SINGLE` = 1 for single-ended inputs; AIN5/6 MUX = A.
4. Set per-channel volumes; clear mutes.
5. Poll status register 0x19 for clock-error / overflow (INT not wired).

---

## 7. Board I/O Summary

| Connector | Pins used | Carries |
|-----------|-----------|---------|
| I²S header | 1, 2, 4, 5, 6, 7, 16, 26, 27 | 5 V, GND, host MCLK/LRCK/BCLK, TDM play (SD_OUT0) + capture (SD_IN0), 3V3 ref |
| CN3 (control) | 5, 6, 7, 8, 9, 10 | SPI (MISO/SCLK/MOSI/SS) + RST# + GND |
| 3.5 mm × 7 | — | 3 stereo inputs, 4 stereo outputs (line level, SE) |

- Single 5 V input (I²S pin 1); on-board 3V3 LDO; no on-board clock; codec configured over SPI (CN3); pure TDM slave.

---

## 8. Open Items / Confirm Before Layout

1. **Host MCLK frequency + Fs** — actual value on I²S pin 4. Sets `MFREQ`, the 256/512× ratio, and bounds ribbon-jitter risk. **Primary open question.**
2. **SPI vs I²C auto-detect** — confirm CS42448 control-port mode-select behavior (datasheet §4.7) for the chosen SPI wiring.
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
