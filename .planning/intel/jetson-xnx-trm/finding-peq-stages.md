# Finding: Tegra194 OPE PEQ Biquad Stage Count

## Answer

**12 biquad stages per channel × 8 channels per OPE PEQ instance** (max 96 biquad
slots per OPE). This matches the devkit ALSA observation
(`OPE1 PEQ Biquad Stages`, INTEGER min=0 max=11 → register value N-1, so up to
12 stages enabled).

## TRM citations (Xavier_TRM_DP09253002_v1.4p.pdf)

- **p2851, §7.8.2.2.9 Output Processing Engine** — "The Output Processing
  Engine (OPE) has scalable number of BiQuad stages to support stereo, 5p1, and
  7p1 channels meeting Ultra-Low Power (ULP) audio requirements." Implies up
  to 8-channel (7.1) routing through one OPE.
- **p3678, §7.8.4.15 PEQ_CONFIG_0** — bits 5:2 `BIQUAD_STAGES`:
  "Number of BiQuad Stages in PEQ Chain (Maximum of 12 - for current arch).
  This number is N-1, example for (max) 12 - it should be 'd11."
  Reset value = 0x4 → 4-bit field value = 4 → effective stage count = 5
  (which matches the kernel's `biquad_init_stage = 5`).
- **p38, l2043** — `OPE1_PEQ` MMIO window `0x02908100..0x029081ff` (one PEQ
  block belonging to OPE1).

## Kernel citations (linux-tegra 5.10.216, scarthgap-l4t-r35.x build)

Driver in active use is `sound/soc/tegra/` (NOT `tegra-alt/` — no PEQ source
exists there; tegra-alt only carries ADSP/admaif/xbar variants).

- `sound/soc/tegra/tegra210_peq.h:34`
  `#define TEGRA210_PEQ_MAX_BIQUAD_STAGES 12`
- `sound/soc/tegra/tegra210_peq.h:36`
  `#define TEGRA210_PEQ_MAX_CHANNELS 8`
- `sound/soc/tegra/tegra210_peq.h:38-41`
  Per-channel gain param block = `2 + 12 * 5 = 62` u32 words (pre-gain +
  post-gain + 5 biquad coeffs × 12 stages); per-channel shift block =
  `2 + 12 = 14` u32 words.
- `sound/soc/tegra/tegra210_peq.c:172-175` ALSA control
  `SOC_SINGLE_EXT("PEQ Biquad Stages", ..., TEGRA210_PEQ_MAX_BIQUAD_STAGES - 1, 0, ...)`
  → max field value 11, matching the `numid=1136` amixer report.
- `sound/soc/tegra/tegra210_peq.c:274, 297, 331` — RAM load/save/init loops
  iterate `i = 0 .. TEGRA210_PEQ_MAX_CHANNELS - 1` (i.e. 8 channels) loading
  gain + shift buffers per channel from AHUB RAM windows. Confirms
  **per-channel** state in silicon, not globally shared.
- `sound/soc/tegra/tegra210_peq.c:32` — `biquad_init_stage = 5` default at
  probe, which is why fresh boots show stage count 5 even though hardware
  supports 12.

## Per-channel vs shared

Each of the 8 channel lanes inside a single PEQ instance has its **own**
coefficient + shift RAM, but the `BIQUAD_STAGES` field in `PEQ_CONFIG_0` is
**one global N for the whole instance** (all 8 channels run the same number
of stages, individually-tuned coefficients). So the effective topology:

- 1 OPE PEQ instance → 8 channels → identical chain length N (1..12) → each
  channel has its own (pre-gain, post-gain, 12×{b0,b1,b2,a1,a2}, shift) RAM.
- For a stereo cabin EQ feeding a 2-ch sink, 6 of those channel lanes are
  unused (still clocked but coefficient RAM is don't-care).

## Caveats

1. **TRM wording "scalable number of BiQuad stages" is generic across Tegra
   family**; the per-SoC concrete maximum is the PEQ_CONFIG bit-field width
   (4 bits, values 0..11 → 1..12 stages). Both TRM (p3678) and the kernel
   define agree on 12 here. No silicon-vs-driver gap.
2. The TRM phrasing "for current arch" hints NVIDIA reserved headroom in the
   register encoding (4-bit field could nominally express up to 16 stages),
   but the silicon's coefficient RAM and pipeline are sized for 12. Don't try
   to write 12..15 to the field.
3. There are two OPE instances in Xavier AHUB (`OPE1`, `OPE2`), each with its
   own independent PEQ + MBDRC. So system-wide budget = 2 × 8 × 12 = 192
   biquad slots if both OPEs are wired into the route.
4. PEQ position relative to MBDRC is configurable (PEQ→MBDRC or MBDRC→PEQ,
   see TRM p3677 l228256-228257). Doesn't affect stage count.

## Conclusion vs prior research doc

Prior claim of "12 biquads" was correct in magnitude but under-specified the
multiplicity. Corrected statement: **up to 12 biquad stages per channel,
8 channels per PEQ, 2 PEQ instances per SoC**. Devkit observation of
`max=11` is the N-1 encoding, not a per-channel cap of 11.
