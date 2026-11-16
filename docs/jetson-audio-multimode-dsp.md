# Multi-Mode Surround + Max-HW DSP — CS42448 / Tegra AHUB

Speaker modes: **2.0, 2.1, 4.1, 5.1, 7.1**. Goal: push every expensive DSP operation into the Tegra194 AHUB silicon (MVC / OPE-PEQ / OPE-MBDRC / SFC / MIXER / AMX) and leave PipeWire only the jobs the hardware genuinely cannot do. Output is the CS42448 (8 DAC ch over I2S5 TDM). See `jetson-cs42448-tdm-plan.md` for the codec bring-up; this doc is the DSP/topology layer on top.

## Key invariant

**I2S5 TDM is always 8 slots, every mode.** CS42448 TDM is fixed 8-slot; the codec/route never reconfigure per mode. A "mode" only changes (a) which slots carry meaningful audio, (b) the upmix matrix that fills them, and (c) the per-channel HW DSP profile. No TDM/route teardown on mode switch — only content + coefficient changes.

## Canonical slot → speaker map (fixed by CS42448 jack wiring)

Breakout jacks: OUT-1=AOUT1/2, OUT-2=AOUT3/4, OUT-3=AOUT5/6, OUT-4=AOUT7/8 → TDM slots 0–7.

| Slot | DAC | Speaker | Jack |
|---|---|---|---|
| 0 | AOUT1 | FL  | OUT-1 T |
| 1 | AOUT2 | FR  | OUT-1 R |
| 2 | AOUT3 | FC  (center) | OUT-2 T |
| 3 | AOUT4 | LFE (sub) | OUT-2 R |
| 4 | AOUT5 | SL  (surround L) | OUT-3 T |
| 5 | AOUT6 | SR  (surround R) | OUT-3 R |
| 6 | AOUT7 | RL  (rear L, 7.1) | OUT-4 T |
| 7 | AOUT8 | RR  (rear R, 7.1) | OUT-4 R |

Lower modes use a subset; unused slots = silence. (WAVE-ish 7.1 order; documented so PEQ banks + PW maps agree.)

| Mode | Active slots (speakers) |
|---|---|
| 2.0 | 0,1 (FL FR) |
| 2.1 | 0,1,3 (FL FR LFE) |
| 4.1 | 0,1,3,4,5 (FL FR LFE SL SR) — "4" = front pair + surround pair |
| 5.1 | 0,1,2,3,4,5 (FL FR FC LFE SL SR) |
| 7.1 | 0–7 (all) |

## Signal chain — identical topology every mode (max HW)

```
  [PipeWire]  decode → resample→48k → matrix-upmix(stereo/src → mode N-ch)
              → per-ch delay (HW has none) → 8-ch S24 PCM
                       │
                       ▼  ADMAIF1 (8-ch, SW→HUB DMA)
  [AHUB / HW] ─ MVC1 ............ master volume (HW-ramped, click-free)
              ─ OPE1 ............ per-channel (8ch):
                                    • PEQ pre_gain   = per-speaker level trim / calibration
                                    • PEQ biquads    = crossover (HP sats / LP→LFE) + room EQ
                                    • MBDRC          = per-band limiter = speaker protection
                                    • PEQ post_gain  = makeup
              ─ I2S5 ........... 8-slot TDM
                       ▼
  [CS42448]   8 DAC → 4 stereo TRS → speakers
```

Order rationale: volume **before** OPE so the MBDRC limiter (inside OPE, last) is post-volume and catches everything → true output protection. Matches the existing `banks-audio-route` order (ADMAIF→MVC→OPE→I2S5).

## What runs where, and why

| Job | Engine | Why |
|---|---|---|
| Decode / format / session | PipeWire | only place it can live |
| Sample-rate convert | PipeWire (default) **or** AHUB **SFC** | PW resampler is flexible; SFC available to offload if CPU-bound. Pick one — don't double-resample. |
| Up/down/fold matrix (route stereo or N-ch → mode slots) | **PipeWire live control-port `filter-chain` matrix** | AHUB MIXER is 10-in × **5-out** (can't emit 8); AMX merges, doesn't matrix. 64-coeff matrix, live-mutable, ~3 M ops/s. |
| Per-channel **delay** (time-align sub/surround) | **PipeWire** (filter-chain `delay`) | AHUB has no delay block. The one unavoidable PW DSP. |
| Master volume | **AHUB MVC1** | HW-ramped, click-free, zero CPU |
| Per-speaker **level trim / calibration** | **AHUB OPE PEQ pre_gain** (per ch) | PEQ blob already carries per-channel `pre_gain`+`post_gain` — free per-speaker gain |
| **Crossover / bass management** (HP satellites, LP→LFE) | **AHUB OPE PEQ biquads** (per ch) | 12 biquad stages/ch — spend 2–4 on xover slopes |
| **Room / voicing PEQ** | **AHUB OPE PEQ biquads** (per ch) | remaining biquads (~8–10/ch) for parametric room correction |
| **Limiter / speaker protection** | **AHUB OPE MBDRC** | the P5' speaker-protection goal, now in silicon not LADSPA |
| LFE channel content (sum/route of mains) | **PipeWire matrix** (coefficient) | matrix derives LFE *content* (mains sum or pass native LFE); HW OPE applies the LP crossover *filter*. PW="what", HW="shape". |
| Future: multi-stream mix / nav-chime ducking | **AHUB AMX** (merge) + **MIXER** (gain duck) | when >1 ADMAIF source needed; merge + duck in HW |

Net: PipeWire = zero-fill + one live matrix (up/down/fold/route/level) + delay. **All frequency shaping, volume, dynamics, protection, and per-speaker calibration = AHUB silicon.** Carmel cost ≈ 64-tap matrix + tiny delay, independent of mode.

## Biquad budget per channel (OPE PEQ, 12 stages)

- Crossover: 1–2 stages (e.g. 2nd–4th-order Linkwitz-Riley HP on satellites, LP on LFE).
- Room/voicing PEQ: remaining ~10 stages as parametric peaks/shelves.
- pre_gain: per-speaker level; post_gain: makeup. (Both in the 62×s32 Q1.30 blob.)
LFE channel gets a steep LP; full-range modes (2.0) skip xover, spend all 12 on room EQ.

## Source ingestion — wireless = stereo, host = multichannel

| Source | Channels | Why |
|---|---|---|
| BT A2DP (phone) | **2** | A2DP is a stereo-only transport (SBC/AAC/aptX/LDAC all 2ch max). No multichannel over BT, ever. |
| Android Spatial Audio | **2** | Virtual head-tracked *binaural* renderer — collapses 5.1/7.1/Atmos to 2ch on the phone (headphone-oriented; disabled/stereo for A2DP speakers). |
| Android Auto | **2** | media audio = stereo PCM (48k/16-bit typical). |
| other USB / network | **2** | stereo clients. |
| **host players** (mpv, gstreamer, local files) | **≤8** | only path that delivers discrete 5.1/7.1. |

→ **Discrete multichannel comes only from host players.** Every wireless/phone source is 2ch. Both land on the same fixed 8-ch sink; the matrix adapts each.

## PipeWire design — fixed 8-ch sink + live control-port matrix

**Topology is fixed; only matrix coefficients change.** Mode switch = a live `pw-cli set-param` coefficient write (same idiom as the OPE PEQ gain writes) — no node recreation, no renegotiation, **glitchless**, stable sink names.

```
clients ─► [ banks_audio : 8-ch sink, channelmix.upmix=false ]   ← never recreated
                 │  pure zero-fill: stereo→ch0/1, 5.1→ch0-5, 7.1→ch0-7, rest 0
                 ▼
           [ matrix filter-chain : 8 in → 8 out, 64 gain controls ]
                 │  per-mode coefficients = upmix + downmix/fold + slot-route + level
                 │  (+ per-channel delay legs — HW has none)
                 ▼
           [ banks_cabin_hw : 8-ch → ADMAIF1 ] ─► MVC ─► OPE ─► I2S5 ─► CS42448
```

- **`channelmix.upmix=false`** on client links → PW does **zero spatial work**, just zero-fills any source into the 8-ch input (absent channels = 0). All spatial math lives in the mutable matrix.
- **Matrix** = 8 builtin `mixer` nodes (one per output slot), each summing 8 inputs via named control gains (`FL:Gain 1..8`, … `RR:Gain 1..8`) = 64 coefficients. Switch writes the new set, ramped ~30 ms to avoid zipper noise. ~3 M mul-add/s on Carmel — trivial.
- **One per-mode matrix handles every source**: because absent input channels are zero, the mode's "7.1-superset → mode speakers" table auto-adapts (stereo, 5.1, 7.1 all fold correctly). Up *and* down mix are just coefficients.
- Per-channel **delay**: `filter-chain` `delay` elements (control = samples), live-settable. The one DSP PW must own (no AHUB delay block).
- Retire the stereo `param_eq` (`99-banks-eq.conf`) for the cabin path — EQ is entirely HW OPE now.

### Up / down / fold coefficient model (ITU/ATSC, C & surround at −3 dB = 0.707)

| Mode | FL out | FR out | sub (LFE) | notes |
|---|---|---|---|---|
| **2.0** | FL +.707·C +.707·SL (+.707·RL) | FR +.707·C +.707·SR (+.707·RR) | dropped (opt. fold L/R @ −10 dB) | no sub; bass via full-range mains |
| **2.1** | same mains fold | same | **LFE → sub** | mains downmixed, sub kept |
| **4.1** | FL +.707·C (+rear fold) | FR +.707·C | LFE → sub | only center folds; SL/SR kept |
| **5.1** | FL (+.707·RL if 7.1 src) | FR (+.707·RR) | LFE → sub | rear folds into surround |
| **7.1** | passthrough | passthrough | passthrough | identity |

- **Upmix** (stereo→surround) = the *same* matrix with distribution coefficients on the surround/center outputs, gated by a per-mode upmix flag (off = stereo→FL/FR only). Optional `psd`-style decorrelation can be a richer coefficient set if wanted.
- **Downmix** (e.g. 5.1→2.0/2.1) = the mode's fold coefficients above; source-adaptive for free (a stereo source's surround inputs are 0, so those terms vanish).
- **LFE-in-stereo** is the one judgment flag per mode: ATSC drops it (sub content returns via bass-managed mains in HW OPE) or fold LFE→L/R at −10 dB to keep LFE-only effects.

## Mode manager — `banks-audio-mode <2.0|2.1|4.1|5.1|7.1>`

Single atomic mode apply (extends `banks-audio-route`), **glitchless** — nothing recreated:
1. **HW**: upload per-channel OPE PEQ banks for the mode (room EQ + crossover), set MBDRC limiter params, MVC master. XBAR route + sinks are mode-invariant (ADMAIF1→MVC1→OPE1→I2S5, 8-slot).
2. **PW**: write the mode's 64 matrix coefficients (live `set-param`, ~30 ms ramp) + per-channel delays. Sink names + topology unchanged → streams keep playing.
3. Persist `/etc/banks-audio/mode`; boot applies last/default.

Profiles on disk:
```
/etc/banks-audio/modes/<mode>/
    route.conf          # MVC, MBDRC, channel-active mask
    peq-ch<0..7>.json   # per-channel PEQ: pre_gain, biquads (xover+room), post_gain
    matrix.json         # 64 PW matrix coefficients (up/down/fold/route/level) + upmix flag + LFE-fold flag
    delay.json          # per-channel delay samples
```
`banks-audio-hw.py` extended: mode-aware, loops the active channels, computes RBJ biquads (room) + Linkwitz-Riley (xover), quantizes Q1.30, uploads per-channel (the per-channel loop already flagged in the bring-up plan), drives MBDRC + MVC. Live per-channel/per-mode tuning retained.

## CS42448 codec-level controls (adjunct to AHUB DSP)

The driver exposes per-channel knobs in the codec itself — secondary to the AHUB DSP but useful:

| Codec control | Use in cabin design |
|---|---|
| `DAC1..4 Playback Volume` (8 ch, −127.5…0 dB) | optional final per-speaker level trim (alt/extra to OPE PEQ pre_gain) |
| `DAC1..4 Invert Switch` (per ch) | speaker polarity fix; **sub phase invert** for room coupling |
| `DAC Soft Ramp & Zero Cross` | click-free codec-side ramp (AHUB MVC already ramps; leave codec immediate) |
| `DAC Auto Mute Switch` | mute on digital silence (consider OFF — can clip program with deep silence) |
| `DAC De-emphasis` / `ADC HPF` | leave default; not needed |

Full driver control list: see `project_audio_cs42448_plan.md` / `jetson-cs42448-tdm-plan.md`. Codec DAI: TDM via **DSP_A only**, S16/S20/S24/S32, 8 playback / 6 capture ch, codec **slave** (Tegra master), MFREQ auto from MCLK/Fs (AUD_MCLK DT rate must equal wired MCLK).

## Engine utilization summary (the "max HW" answer)

| AHUB block | Used for | Modes |
|---|---|---|
| ADMAIF1 | 8-ch SW→HUB DMA | all |
| MVC1 | master volume (HW ramp) | all |
| OPE1 PEQ | per-ch level + crossover + room EQ | all (2.0 = room EQ only) |
| OPE1 MBDRC | per-band limiter / speaker protection | all |
| I2S5 | 8-slot TDM out | all |
| SFC | optional HW resample | when source ≠48k & offload wanted |
| MIXER | optional LFE sum / future ducking | 2.1+ optional |
| AMX | future multi-stream merge | future |

Idle silicon under 2.0: surround/center/LFE slots silent, their OPE channels bypassed — but the path is unchanged, so escalating to 7.1 is a coefficient/matrix swap, never a re-architecture.

## Locked
- **Runtime-switchable, glitchless** — fixed 8-ch sink + live control-port matrix; mode switch = `set-param` coeff write, no node recreate.
- **Single 8-ch sink** (`banks_audio`) for all sources; `channelmix.upmix=false` zero-fill; one per-mode 64-coeff matrix does up/down/fold/route/level.
- **Dual source classes**: wireless (BT/AA/Spatial/USB) = always 2ch; host players = native ≤8ch. Both hit the same sink, matrix adapts.
- **Resample owner = PipeWire** (per-stream, graph edge). Sources arrive mixed-rate and mix *before* output; AHUB SFC sits in the single shared output path so it can't resample per-source. PW sinc resampler, trivial CPU for stereo. SFC reserved, unused.
- **Slot→speaker order** = FL FR FC LFE SL SR RL RR (slots 0–7) → jacks OUT-1 mains / OUT-2 center+sub / OUT-3 surround / OUT-4 rear. Internal labels are our choice (matrix maps PW positions explicitly); wire jacks to match. Confirm only the physical jack→cabin-speaker wiring.
- **Host MC player = mpv** (`--ao=pipewire --audio-channels=7.1`) for standalone/test; gstreamer arrives implicitly via Qt frontend (QtMultimedia → pipewiresink). Package mpv when validating multichannel (SW4-phase, not a SW1–3 blocker).

## Commissioning defaults (runtime config, tune on hardware)
These ship as starting values in `/etc/banks-audio/modes/<mode>/`; dial in per install with a measurement mic. None block code.

| Knob | Default | Flag / tune |
|---|---|---|
| **Crossover** (sat HP / sub LP) | **80 Hz LR4** (Linkwitz-Riley 4th) | per-mode/per-channel in `peq-ch*.json`; lower for door mains, 80–120 Hz sub |
| **Upmix** (surround modes, stereo src) | **simple distribution matrix** (surround = L/R reduced, center = 0.707·(L+R)) | per-mode flag → `psd` (decorrelated, more envelopment) or `none`. 2.0/2.1 = no surround synth |
| **LFE-in-stereo** (2.0, 5.1 src) | **fold LFE→L/R @ −10 dB** | flag → `drop` (ATSC) if mains are small |
| **Per-channel delay** | **0** | measure path-length Δ at install (Δdist / 343 m/s), set `delay.json` |

## Pending confirm
- **RESET GPIO** = `soc_gpio41_pq5` (`TEGRA194_MAIN_GPIO(Q,5)`) = **40-pin header pin 29** (confirmed free, GPIO-muxed). Alts: pin 31/32/33.
- **Jack→speaker physical wiring** matches the slot map above.

## Risks / notes
- MVC is a single master gain on the multichannel node (per-channel level lives in OPE PEQ pre_gain instead — confirmed by the 62-word blob layout).
- MBDRC multiband config + per-channel PEQ upload via ctypes `snd_ctl_elem_write` (amixer can't write the arrays) — see `project_audio_hw_eq.md` gotchas.
- DAPM keepalive must hold the 8-ch substream open for PEQ-RAM writes.
- Per-channel delay is the sole forced-PW DSP; everything else is silicon.
- Capture (6 ADC in) out of scope here (deferred per user).
