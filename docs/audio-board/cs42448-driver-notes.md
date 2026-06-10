# CS42448 — driver vs datasheet notes

Driver: mainline `sound/soc/codecs/cs42xx8.{c,h}` (Freescale/Nicolin Chen),
generic for CS42448 + CS42888. As shipped in linux-tegra 5.10 kernel-source.
Datasheet: `docs/audio-board/CodecBoardData/CS42448_F5 (1).pdf` (DS648F5, Rev A).

Verified MATCH: all 27 reg addresses (01h–1Bh), bit fields, chip-ID gate
(top nibble 0000), MFreq ratio table, DIF encodings, volume TLV, channel
counts (6-in/8-out, num_adcs=3, AIN5/6 MUX + ADC3 gated to cs42448).

## Deviations from datasheet

### D1 — reg 0x04 (INTF) regcache default wrong  [`cs42xx8.c:407`]
Driver seeds `{ 0x04, 0x46 }`. DS §5/§6.5 power-on reset = **0x36**.

| field            | DS POR        | driver 0x46    |
|------------------|---------------|----------------|
| FREEZE   [7]     | 0             | 0          ✓   |
| AUX_DIF  [6]     | 0 (LeftJ)     | 1 (I²S)    ✗   |
| DAC_DIF  [5:3]   | 110 (TDM)     | 000 (LeftJ) ✗  |
| ADC_DIF  [2:0]   | 110 (TDM)     | 110 (TDM)  ✓   |

Impact: HARMLESS on this board. DAC_DIF/ADC_DIF always overwritten by
`set_dai_fmt`; AUX_DIF never written by driver and AUX serial port unused.
Net effect = regcache permanently disagrees with HW on AUX_DIF bit (cache
thinks 1, HW reset is 0), never read → no functional consequence. Still a
factual upstream bug; not ours.

### D2 — sample-rate boundary gaps  [`cs42xx8.c:287-297`]
```
rate < 50000              -> single
rate > 50000 && < 100000  -> double
rate > 100000 && < 200000 -> quad
else                      -> -EINVAL
```
Strict `<`/`>`. Exactly 50000 / 100000 / 200000 hit no branch -> spurious
`-EINVAL`. DS §6.3 (and reg 03h desc) boundaries are inclusive
(SSM 4–50, DSM 50–100, QSM 100–200 kHz).

Impact: only fires in codec-MASTER mode (`CBM_CFM`). In slave mode
(`slave_mode=true`) fm=FM_AUTO and the whole check is skipped. On Tegra the
I2S controller is master -> codec is slave -> never hit. Plus 50/100/200k
are uncommon. Low impact, latent.

Both fixable via a linux-tegra bbappend patch if ever needed (not applied).

---

## 8-channel / 32-bit-slot / TDM / 256× — required chip config

Source: DS §4.5.6 (TDM), §4.5.7 (channel alloc, Table 9), Table 8 (TDM clock
ratios), Table 11 (MCLK for TDM), §4.3.1 (init), reg 02h/03h/04h desc.

### Hard facts / gotchas
- **TDM is SLAVE-ONLY** (Table 8: master mode N/A). Tegra I2S must be the
  clock master. Codec auto-detects speed mode from MCLK/LRCK.
- **TDM slot = 32 SCLK wide**, but **valid data length max 24 bits**
  (DS §4.5.6: "Valid data lengths are 16, 18, 20, or 24"). Sample is
  left-justified (MSB-first) in the 32-bit slot. So feeding S32_LE gives
  **24-bit effective** resolution (low 8 bits ignored). There is NO true
  32-bit audio mode — "32-bit" = the slot/container, not the sample.
- **SCLK must = 256·Fs** in TDM (mandatory). 8 slots × 32 clk = 256 clk/frame
  = 256Fs. So "256× TDM" is the only TDM SCLK ratio that exists.
- **8 channels ride one data line**: in TDM, DAC_SDIN1 carries AOUT1–8;
  ADC_SDOUT1 carries AIN1–6 + AUX1–2 (Table 9). SDIN2/3/4 unused.
- MCLK ≥ SCLK. At Fs=48k, SCLK=256Fs=12.288 MHz. If MCLK also 256Fs
  (=12.288 MHz) -> MFREQ=000 (Table 11 range 1.029–12.8 MHz, SSM ratio 256).

### Register writes (Fs=48k, MCLK=12.288 MHz, slave/TDM, 24-bit-in-32-slot)
Per DS init flow (Fig 12): RST high -> set up registers BEFORE MCLK ->
change DAC pairs only while muted or PDN=1.

| order | reg            | value | meaning                                        |
|-------|----------------|-------|------------------------------------------------|
| 1     | 02h PWRCTL     | 0x01  | PDN=1 — hold powered down during config        |
| 2     | 07h DACMUTE    | 0xFF  | mute all 8 DAC channels                        |
| 3     | 03h FUNCMOD    | 0xF0  | DAC_FM=11, ADC_FM=11 (slave/auto), MFREQ=000   |
| 4     | 04h INTF       | 0x36  | FREEZE=0 AUX=0, DAC_DIF=110 TDM, ADC_DIF=110 TDM|
| 5     | 06h TXCTL      | 0x10  | AMUTE on (POR default); set SZC as desired     |
| 6     | 02h PWRCTL     | 0x00  | PDN=0, all 4 DAC + 3 ADC pairs enabled         |
|       | (apply MCLK, then LRCK/SCLK; ~2000-LRCK + 400ms VQ ramp)     |
| 7     | 07h DACMUTE    | 0x00  | unmute all 8 channels once clocks stable       |

MFREQ note: MCLK=512Fs (24.576 MHz, SCLK still 256Fs) -> use MFREQ=010
(Table 11). MCLK=256Fs -> MFREQ=000. "256×" target = MFREQ=000.

### Does the mainline driver produce this set? — YES
Drive it with: DAI fmt `SND_SOC_DAIFMT_DSP_A` + `CBM_CFM` (Tegra=master,
codec=slave), `set_sysclk`=12288000, 8ch `S32_LE` @48k.
- 04h: `set_dai_fmt` DSP_A -> DAC_DIF=ADC_DIF=110 (TDM). ✓ (real HW = 0x36)
- 03h: `hw_params` ratio=12.288M/48k=256 -> ratios[0].mfreq=0 (MFREQ=000);
  slave_mode -> fm=FM_AUTO (11). ✓
- 02h: DAPM powers PWR + DAC1–4. ✓
- 07h: probe mutes all (0xFF); `cs42xx8_mute` with tx_channels=8 ->
  `~((1<<8)-1)` on u8 = 0x00 -> unmute all 8. ✓
- Rate-gap bug (D2) not hit: slave mode + 48k<50000.
- D1 (0x46 default) not hit functionally: DIF overwritten, AUX unused.

Required from the Tegra/machine side: I2S as TDM master, 8 slots × 32 bit,
BCLK=256Fs=12.288 MHz, FSYNC=Fs, MCLK routed to codec at 256Fs (12.288 MHz),
codec `reset` GPIO + `mclk` clock + VA/VD/VLS/VLC regulators wired in DT.
Note DS §Note 19: in TDM, VLS limited to 2.5–5.0 V.

---

# Tegra194 (Xavier NX) clock + I2S path for this codec

Synthesis of TRM + kernel-source + actual repo DT. Branch `feat/devkit-i2s-audio`.
Confidence tags: [CODE]=read from kernel source, [DT]=read from repo/NVIDIA DT,
[TRM]=Tegra194 TRM, [INFER]=reasoned, not directly confirmed.

## Bottom line
The board is ALREADY wired and the software path ALREADY exists — this is not a
greenfield bring-up. We are NOT poking registers or hand-rolling a machine
driver. The job reduces to DT + letting NVIDIA's `tegra186-ape` ASoC stack +
`tegra210-i2s` + BPMP do the clock/register work. At 48k it produces exactly the
256× config the codec needs.

## Topology (all [DT]/[CODE] confirmed)
- I2S instance: **I2S5 / DAP5**. `i2s5_to_codec` link = the 40-pin-header I2S link
  on p3668 (`nvidia/.../jakku/.../tegra194-audio-p3668.dtsi`: `hdr40_snd_link_i2s:
  &i2s5_to_codec {}`). Our `cs42448.dtsi` overrides that link.
- Active card: **`nvidia,tegra186-ape`** (NVIDIA APE machine driver:
  `sound/soc/tegra/tegra_machine_driver.c` + `tegra_asoc_utils.c` + `tegra_codecs.c`),
  card prop **`nvidia-audio-card,mclk-fs = <256>`**.
  - The `audio-graph-card` path (and the endpoint `mclk-fs=<512>` on line 82 of
    cs42448.dtsi) is **INERT / disabled** — ignore it. The graph-card DT sketches
    some research produced do NOT apply.
- Codec MCLK source: `aud_mclk_ps4` pad → 40-pin header **pin 7**. Codec DT:
  `clocks=<&bpmp_clks TEGRA194_CLK_AUD_MCLK>`, `clock-names="mclk"`.
- Pinmux owned by **MB1** (kernel pinmux disabled on XNX); DAP5 sclk/fs/dout/din +
  aud_mclk already muxed in `tegra19x-mb1-pinmux-p3668-a01.cfg`. No DT pinmux frag.

## Clock tree — exact rates, both sample-rate families  [CODE: tegra_asoc_utils.c]
T194 clocks are **BPMP-firmware-owned**; kernel only uses CCF
`clk_set_rate()/clk_set_parent()` → BPMP ABI. No direct CAR register writes. [TRM ~p5372]

```
OSC ─ PLLA ─ PLLA_OUT0 ┬─ clk_cdev1 (=AUD_MCLK)  ──► codec MCLK pin7
   (BPMP, frac-N)       └─ i2s5 functional clk    ──► I2S5 BCLK (+ FSYNC=BCLK/256)
```

|                         | 48 kHz family | 44.1 kHz family |
|-------------------------|---------------|------------------|
| PLLA                    | 368.640 MHz   | 338.688 MHz      |
| PLLA_OUT0 (÷7.5 frac-N) | 49.152 MHz    | 45.1584 MHz      |
| i2s5 clk = BCLK (÷4 int)| 12.288 MHz    | 11.2896 MHz      |
| AUD_MCLK / cdev1 (÷4 int)| 12.288 MHz   | 11.2896 MHz      |
| FSYNC = BCLK/256        | 48 kHz        | 44.1 kHz         |
| cs42xx8 ratio = mclk/Fs | **256 exact** | **256 exact**    |

- Below PLLA_OUT0 everything is a clean **integer ÷4** → no truncation; 12.288 /
  11.2896 MHz land exactly. cs42xx8's integer `ratio = sysclk/rate` = 256 exactly,
  inside table entry `{mfreq=0, 1.029–12.8 MHz, {256,128,64}}` and min/max gate. ✓
- 44.1k family needs a PLLA **re-lock** (368.64→338.688). `tegra_asoc_utils` does
  this per-family automatically; runtime 48k↔44.1k switch = brief reconfigure.
- BCLK and MCLK share PLLA parent → jitter-coherent. [INFER but follows from tree]

## Who sets what, in call order  [CODE]
1. `tegra_machine_driver.c:124 tegra_machine_dai_init()` (from hw_params)
   → `:166 tegra_asoc_utils_set_tegra210_rate(srate, ch, sample_size)`.
2. `tegra_asoc_utils.c:254` pick PLLA base (368.64M @48k fam) →
   `:272 clk_set_rate(clk_pll_a)`, `:282 clk_set_rate(clk_pll_a_out0, 49.152M)`,
   `:293 aud_mclk = srate*mclk_fs(256)`, `:296 clk_set_rate(clk_cdev1, aud_mclk)`.
   (`clk_cdev1` ⇒ `TEGRA194_CLK_AUD_MCLK`.)
3. `tegra_codecs.c:184 snd_soc_dai_set_sysclk(codec, aud_mclk, SND_SOC_CLOCK_IN)`
   → `cs42xx8_set_dai_sysclk` stores 12.288M (overrides the probe-time
   `clk_get_rate`).
4. `tegra210_i2s.c` `set_clock_rate()` → `clk_set_rate(clk_i2s, bclk)` where
   `bclk = srate*channels*sample_size` = 48000·8·32 = 12.288 MHz. BCLK ÷256 = FSYNC.
5. `cs42xx8_hw_params`: `ratio = sysclk/rate = 256`, validated, MFREQ=0 programmed.

## I2S5 TDM register programming — done by `tegra210-i2s`, NOT by us  [CODE/DT/TRM]
Driven entirely from DT (`format="dsp_a"`, `bitclock-master`, `frame-master`,
`fsync-width=<0>`) + codec/machine `set_tdm_slot(8 slots, 32-bit)`. For reference,
the driver lands roughly (I2Sn base 0x02901000+n·0x100):
- MASTER=1, FRAME_FORMAT=FSYNC(TDM), BIT_SIZE=32, TOTAL_SLOTS=7 (8 slots),
  SLOT_ENABLES=0xFF, CHANNEL_BIT_CNT=255 (256 BCLK/frame), CIF bits/chans=32/8.
- **FSYNC_WIDTH:** DSP_A ⇒ **1-bclk frame pulse**, set via DT `fsync-width=<0>`.
  (One research pass claimed FSYNC_WIDTH=32 — that's the I2S/left-justified
  half-frame style and is WRONG for DSP_A/TDM. Real DT = 0.)
- Enable order tail→source (I2S → ADMAIF → ADMA); disable source→tail. [TRM p2862]
- AHUB/XBAR mux that connects ADMAIF↔I2S5 TX/RX: not documented in TRM register
  tables; handled by the ASoC AHUB/XBAR driver + DT routing. [TRM gap, CODE-handled]

## Data path  [TRM p2834+]
Playback: ADMA → ADMAIF (0x0290f000) → AXBAR/AHUB crossbar → I2S5 TX → DAP5 pins.
Capture (deferred): DAP5 → I2S5 RX → AXBAR → ADMAIF → ADMA. DMIC/DSPK irrelevant.

## "32-bit" reminder (codec limit, repeated because it matters)
TDM slot = 32 BCLK, but CS42448 valid data ≤ **24-bit** (left-justified). Feeding
S32_LE = 24-bit effective. Tegra still clocks 32-bit slots (BCLK=256Fs); the low
8 bits are don't-care into the codec. No true 32-bit audio.

## Open items / verify on hardware
- [ ] Confirm `mclk-fs=256` actually wins at runtime (it's the `tegra186-ape`
      card prop; endpoint 512 is inert) — check `aud_mclk` rate after stream start
      (`cat /sys/kernel/debug/clk/.../clk_rate` or `clk_summary`).
- [ ] Confirm `tegra210-i2s` slot/width come out 8×32 (BCLK=12.288M on scope/clk_summary).
- [ ] Capture (ADC) path: needs card routing/widgets + the board's DAC↔ADC clock
      bodge (per design doc); playback needs none.
- [ ] cs42xx8 0x04 default (D1) + rate-gap (D2) deviations are benign here
      (slave mode, DIF overwritten) — no action unless codec-master ever used.
- Refs: docs/cs42448-devkit-design.md, docs/jetson-cs42448-tdm-plan.md,
  docs/jetson-audio-multimode-dsp.md.

---

# APE ADSP — verdict: disabled (useless on this image)

Decision: **ADSP fully disabled.** It provides zero usable audio function on
this secure-boot image, so disabling is pure gain. Real DSP stays in the AHUB
fabric (OPE/MVC/SFC/MIXER) + CPU/PipeWire.

## The 6 ADSP plugins (live-tested 2026-06-01, devkit)
Source: `.planning/intel/jetson-xnx-trm/finding-adsp-plugins-live.md`,
`docs/nvadsp-custom-plugin-feasibility.md`.

| Plugin | Widget | ELF | Function | Live? |
|--------|--------|-----|----------|-------|
| mp3-dec1 | MP3-DEC1 | nvmp3dec.elf | MP3 decode | ✗ |
| spkprot | SPKPROT-SW | nvspkprot.elf | speaker protection | ✗ |
| src | SRC | nvsrc.elf | sample-rate convert | ✗ |
| aac-dec1 | AAC-DEC1 | nvaacdec.elf | AAC decode | ✗ |
| aec | AEC | nvoice.elf | echo cancel / voice | ✗ |
| **wire** | WIRE | libnvwirefx.elf | lossless passthrough | ✓ |

Plus framework apps (not DSP): apm, adma, adma_tx, nvadma, nvadma_tx,
adma_test, adspff, adsp_lpthread, adsp_submit, csm_sm, mods_app, nvapm.

## Why 5/6 are dead — and where the blobs come from
- Plugin ELFs are NOT standalone files. They are **statically baked into the
  encrypted, OEM-signed `adsp-fw.bin`** at NVIDIA build time, loaded by
  `load_adsp_static_apps()` — not via rootfs `request_firmware`. `/lib/firmware/
  *.elf` is empty on the live board.
- Our `adsp-fw.bin` (392 KB, from L4T R35.6.4 `Linux_for_Tegra/bootloader/`, the
  same blob SDK Manager flashes) bakes in only **wire + framework**. The other 5
  are simply not in it. The DT advertises 6 plugin slots with fw_names, but those
  fw_name references are vestigial.
- 4 walls block adding them (any one fatal): (1) no standalone distribution for
  R35.6.4 — not in meta-tegra/L4T debs/public sources; (2) `adsp_os_secload=1`
  blocks dynamic ELF load (`nvadsp/app.c:319`, `tegra194-soc-audio.dtsi:90`);
  (3) replacing the blob needs the OEM signing key; (4) even a custom dynamic
  plugin can't resolve ADSP-OS symbols on L4T (`create_global_symbol_table` is
  `#ifdef CONFIG_ANDROID`, never built). NVIDIA's official line: "ADSP is not
  currently supported by L4T."
- Even the controls are useless: tuning is an opaque NVIDIA-proprietary TLV blob
  via `<WIDGET> set params`; the layout lives in closed `nvaudio` headers not in
  any open package.

## How it's disabled (matched kernel + DT pair — BOTH required)
- Kernel `audio-soc.cfg`: `# CONFIG_TEGRA_NVADSP is not set` +
  `# CONFIG_SND_SOC_TEGRA210_ADSP is not set`. (Nothing `select`s them, so the
  unsets hold. AHUB/I2S/ADMAIF/accelerators only depend on `SND_SOC_TEGRA`, not
  ADSP — they stay.)
- DT `cs42448.dtsi` (both A203 + devkit copies): `&tegra_adsp_audio { status =
  "disabled"; };`.
- **Why both:** the tegra186-ape card's DT link list includes ~ADSP PCM/COMPR/FE
  dai-links bound to `&tegra_adsp_audio`. With the ADSP driver removed,
  `of_dai_link_is_available()` (`tegra_asoc_machine.c:39`) still sees those links
  as available (it checks the sound-dai node's `status`) → the missing component
  → `snd_soc_register_card` EPROBE_DEFERs forever → **the whole card incl. the
  codec dies.** Marking `tegra_adsp_audio` disabled makes the check skip every
  ADSP link, so the card registers with just ADMAIF + I2S5 + AHUB accelerators.
- Kernel-only disable WITHOUT the DT edit = broken card. DT-only without the
  kernel edit = ADSP still loads. They are a pair.

## What disabling gains (codec + OPE/MVC/SFC/MIXER all kept)
1. Boot: no nvadsp ADSP-OS firmware load (blocks audio bringup ~hundreds of ms).
2. Log: kills tegra210-adsp "Broken Path - FE not linked to BE" spam (~14 msgs,
   ~1.5–3 s churn) + 5x "Failed to init app …elf" + PipeWire "Broken pipe" churn
   over the ~26 ADSP dummy DAIs.
3. Memory: frees nvadsp.ko + snd-soc-tegra210-adsp.ko + ADSP DRAM carveout
   (~580 KB) + ARAM reservation.
4. Smaller ALSA surface: drops ADSP PCM/COMPR devices + the 6 dead plugin
   MUX/params/bytes controls + ADSP-ADMAIF DAPM widgets → faster card init.
5. Fewer modules at coldplug.

## What is lost
Only the WIRE passthrough probe = nothing usable. Net: pure gain.

## If ADSP is ever wanted back
Re-add the two configs to audio-soc.cfg and drop the `&tegra_adsp_audio
status=disabled` from cs42448.dtsi. (Pointless unless NVIDIA ships a richer
`adsp-fw.bin` — not available for R35.6.4/t194; JP6 dropped Xavier NX.)

---

# BUG + FIX: "unsupported sysclk ratio" — codec never told its MCLK

Observed live on the board (10.0.10.171), playback fails:
```
cs42xx8 1-0048: unsupported sysclk ratio
ASoC: error at snd_soc_dai_hw_params on cs42448: -22
tegra-asoc: sound: ASoC: PRE_PMU: cs42448-tdm-playback event failed: -22
```

## Root cause
`tegra_codecs_runtime_setup()` (`sound/soc/tegra/tegra_codecs.c`) forwards the
audio master clock to the codec via `snd_soc_dai_set_sysclk()` ONLY for a
hard-coded allowlist: `rt565x-playback`, `rt5640-playback`, the rt PLL
variants, and `dspk-playback-dual-tas2552`. **cs42448 is absent.** So the
cs42xx8 codec never learns its MCLK rate and keeps the stale value read once at
probe via `clk_get_rate("mclk")`. `cs42xx8_hw_params()` then computes
`ratio = sysclk / rate`, fails to match `cs42xx8_ratios[]`, and returns
`-EINVAL`.

The Tegra side is healthy — verified on the board: `aud_mclk = 12287996`
(≈256·48k), `pll_a_out0 = 49.152M`. The clock is correct; it just isn't
propagated to the codec. (And `tegra_codecs` passes the *requested* clean
`aud_mclk = srate·mclk-fs = 12288000`, so the fix gives `ratio = 256` exactly —
the 4 Hz PLLA fractional error is irrelevant.)

## Fix
Patch `0003-tegra_codecs-set-cs42448-sysclk.patch` (in `linux-tegra` SRC_URI,
gated to a203 + devkit) adds a `cs42448-tdm` case mirroring rt565x:
```c
rtd = get_pcm_runtime(card, "cs42448-tdm");
if (rtd) {
    err = snd_soc_dai_set_sysclk(rtd->dais[rtd->num_cpus], 0,
                                 aud_mclk, SND_SOC_CLOCK_IN);
    ...
}
```
`"cs42448-tdm"` = the DT `link-name` (→ `dai_link->name`, `tegra_asoc_machine.c:488`).
cs42xx8 ignores clk_id and just stores freq. Lands in
`snd-soc-tegra-machine-driver.ko`.

## Deploying without a full flash (answer to "can the kernel be pushed?")
The board's FLASHED `extlinux.conf` DOES carry an `FDT` line
(`FDT /boot/devicetree/tegra194-p3668-a203.dtb`) even though the rootfs
template doesn't — meta-tegra bootfiles emit it into the boot partition. So
kernel `Image`, the DTB, and `/lib/modules/<uname>/` all live on the NVMe
rootfs the board boots from and are scp-able; no flash needed.
- This fix touches only `tegra_codecs.c` → push the rebuilt
  `snd-soc-tegra-machine-driver.ko` to `/lib/modules/5.10.216-l4t-r35.6.4+g050b88cc9d0f/`,
  `depmod -a`, reboot (or rmmod/modprobe the audio stack). DTB + Image unchanged
  by this patch.
- uname matches the build (`5.10.216-l4t-r35.6.4+g050b88cc9d0f`) → vermagic OK.

## After deploy — verify
- `dmesg | grep cs42` clean of "unsupported sysclk ratio".
- `speaker-test -D hw:APE,0 -c 8 -r 48000` runs without `-EINVAL`.
- Then check the BCLK: live `i2s5` clk read 24.576 MHz (512·Fs) — confirm on
  scope whether the I2S block halves it to 256·Fs at the pin (TDM needs
  SCLK=256·Fs); if not, that's the next thing to chase.
