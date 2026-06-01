# Custom NvADSP plugin on Tegra194 / Xavier NX (L4T R35.6.4) — Feasibility

Investigation date: 2026-06-01. All citations are to source paths within
this repo / kernel source tree, or to NVIDIA / forum URLs. Nothing here
should be taken on trust — every claim has a pointer.

---

## Executive verdict

**Unrealistic without an NDA + production-fused board.** The end-to-end
path "compile a Cortex-A9 ELF, drop it in `/lib/firmware`, kernel loads
it into ADSP DRAM" is *implemented* in the L4T host driver — but on
Tegra194 it is **gated off** by `nvidia,adsp_os_secload` (present in
NVIDIA's own `tegra194-soc-audio.dtsi`) and by the secure-boot
requirement that only an OEM-signed `adsp-fw.bin` can be loaded by
BPMP/MB1 before the kernel even probes. The user-facing plugin SDK,
plugin reference sources (`nvwirefx`, `nvspkprot`, etc.), Cortex-A9
toolchain, and linker script that NVIDIA uses internally are **not
public, not in meta-tegra, not in the BSP `public_sources` tarball, not
mentioned in L4T docs.** NVIDIA staff have stated explicitly on the
developer forum (more than once, across TX1 / TX2 / Xavier NX) that
"the ADSP is not currently supported by L4T for Jetson and therefore,
it is not possible to use" custom firmware. Room correction / FIR
convolution should ship as a PipeWire `filter-chain` running on the A57
cluster (cost: ~3-5% of one core for 8192-tap stereo @ 48 kHz with
`convolver`/`convolution_sofa`), or as a kernel-side LADSPA in the
existing `99-banks-eq.conf` drop-in pattern. The A57 cores have
unused headroom; the engineering effort to liberate the ADSP — even
if NVIDIA opened the SDK tomorrow — is at least an order of magnitude
larger than the CPU cost it would save.

---

## 1. NvADSP "SDK" availability & license posture

### What's open in the L4T R35.6.4 source

The **host-side** ADSP framework is fully open in the L4T kernel source
(GPLv2, NVIDIA copyright):

- `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/drivers/platform/tegra/nvadsp/`
  — 30+ files, ~13k LOC: ELF loader (`app_loader_linker.c`), app
  lifecycle (`app.c`), OS load / suspend (`os.c`, `os-t18x.c`), HSP
  hardware mailbox (`hwmailbox.c`, `hwmailbox.h`), msgq circular
  buffers (`msgq.c`), DRAM heap (`dram_app_mem_manager.c`), ARAM
  exclusive heap (`aram_manager.c`), userspace knock-on
  (`adsp_console_dbfs.c`, `adspff.c` filesystem RPC, `acast.c`).
- `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/include/linux/tegra_nvadsp.h`
  — 402 lines, the in-kernel API. Covers shared semaphores, arbitrated
  semaphores, mailbox queues, msgq, DRAM/IOVA mapping, OS lifecycle,
  app lifecycle (`nvadsp_app_load` / `_init` / `_start` / `_stop` /
  `_unload`), ARAM allocator. This is consumer-facing only — there is
  no ADSP-side header.
- `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/sound/soc/tegra-alt/tegra210_adsp_alt.c`
  — 4790 lines. The ASoC driver that hosts plugins as DAPM widgets.
  All RPC message structs (`apm_msg_t`, `apm_shared_state_t`) live
  here and in `tegra210_adsp_alt.h`. This is the actual ADSP <-> host
  ABI the plugins must speak.

### What is NOT open

| Artifact | Status | Evidence |
|---|---|---|
| ADSP-side runtime (the FreeRTOS-derived OS the A9 boots) | Closed, signed | `build/tmp/sysroots-components/jetson_xavier_nx_banks_devkit/tegra-bootfiles/usr/share/tegraflash/adsp-fw.bin` is a 384 KB encrypted blob. `hexdump` shows ARM-vector-table prologue then high-entropy ciphertext; `strings` returns only the XZ-decompressor stub error messages, nothing else. |
| Plugin ELFs (`nvwirefx`, `nvspkprot`, `nvmp3dec`, `nvsrc`, `nvaacdec`, `nvoice`) | Closed; **shipped baked into the encrypted `adsp-fw.bin`** | Live board `/lib/firmware/*.elf` is empty (verified via `jtx ssh ls /lib/firmware`). dmesg shows `nvadsp 2993000.adsp: ADSP OS firmware already loaded` and then `Loaded app wire / apm / adma / adma_tx` — the apps come from the ADSP side via `load_adsp_static_apps()` (`nvadsp/app.c:951`), not from rootfs. |
| Plugin source code | Closed | No `nvwirefx*` / `nvspkprot*` source anywhere in meta-tegra, kernel-source, `Jetson-public_sources-35.6.4.tbz2`, or any L4T deb. `Jetson-public_sources-35.6.4.tbz2` lists: kernel, ATF, gstreamer plugins, OpenWFD samples, OpenCV samples, optee — **no ADSP samples.** |
| Toolchain (cross-compiler, linker script for the A9 ADSP) | Closed | Same. Not referenced by any meta-tegra recipe. |
| Documentation of the plugin C ABI (entry symbols, init/process callback prototypes) | Closed | L4T R35.6.4 dev guide section `Audio Setup and Development` (`docs.nvidia.com/jetson/archives/r35.6.4/DeveloperGuide/SD/Communications/AudioSetupAndDevelopment.html`) covers ASoC platform/codec/machine only. No plugin-authoring section. |

### NVIDIA's stated position (multiple forum threads)

- "Unfortunately, the ADSP is not currently supported by L4T. It is
  good to hear that people are interested in this and so we can
  consider for the future but for now there are no immediate plans
  to support this." — Jonathan H., NVIDIA staff, on
  https://forums.developer.nvidia.com/t/running-code-on-ape-adsp/70439
  (Jetson TX1). Identical wording reused for Xavier NX in
  https://forums.developer.nvidia.com/t/jetson-ape-adsp-fw-customization/171019.
- "Only OEM signed adsp-fw.bin can be used. Any different firmware
  must be signed with the same key." — ShaneCCC, NVIDIA staff, on
  https://forums.developer.nvidia.com/t/how-to-take-ape-audio-processing-engine-out-of-reset-on-the-jetson-tx2/53507
  (Jetson TX2, T186). Xavier NX is T194 and inherits the same secure
  boot path.

### License contamination

Moot until the SDK exists. If NVIDIA ever releases it, expect the same
posture as the rest of the NVIDIA L4T components: EULA-restricted
binary distribution with permission to link your own object code, no
re-distribution of derived works including their proprietary headers.
Plan to keep our plugin source in a separately-licensed tree.

---

## 2. ABI reverse-engineered from `tegra210_adsp_alt.c` + the nvadsp framework

This is what we *know for certain* about the host <-> plugin contract.
Anyone attempting to write a custom plugin must satisfy all of this.

### 2.1 Plugin ELF format (when dynamic loading is enabled)

From `app_loader_linker.c`:

- **32-bit ARM ELF relocatable object** (`elf32_hdr`, not an
  executable). Architecture: Cortex-A9 (ARMv7-A) + NEON + VFPv3.
- Relocation types the loader understands: `R_ARM_NONE`, `R_ARM_ABS32`,
  `R_ARM_TARGET1`, `R_ARM_PC24`, `R_ARM_CALL`, `R_ARM_JUMP24`,
  `R_ARM_V4BX`, `R_ARM_PREL31`, `R_ARM_MOVW_ABS_NC`, `R_ARM_MOVT_ABS`,
  `R_ARM_THM_CALL`, `R_ARM_THM_JUMP24`, `R_ARM_THM_MOVW_ABS_NC`,
  `R_ARM_THM_MOVT_ABS`. (lines 101-115)
- Must be compiled `-fno-common` (line 460 errors otherwise).
- Sections the loader treats specially (line 900-936):
  - `.dram_data` -> sized into `mem_size.dram` (per-instance, cached)
  - `.dram_shared` -> `mem_size.dram_shared` (uncached, shared with host)
  - `.dram_shared_wc` -> `mem_size.dram_shared_wc` (write-combined)
  - `.aram_data` -> `mem_size.aram` (preferred ARAM; falls back to DRAM)
  - `.aram_x_data` -> `mem_size.aram_x` (exclusive ARAM)
- Everything else with `SHF_ALLOC` is `memcpy`'d into a single DRAM
  allocation returned by `dram_app_mem_request()`
  (`move_module()`, line 507). Load address is dynamic, picked by
  the ADSP-DRAM heap allocator (the ~580 KB plugin DRAM window you
  saw in `/proc/iomem`). Relocations are applied against this
  runtime-chosen base. No fixed link address.
- Symbol resolution: undefined symbols are looked up in
  `priv.adsp_glo_sym_tbl` populated by `create_global_symbol_table()`
  in `os.c:448` — but **that function is wrapped in `#ifdef
  CONFIG_ANDROID`**. In L4T mainline Linux it's never called.
  Implication: on L4T, a dynamic plugin cannot reference any symbol
  exported by the ADSP OS; only symbols defined inside its own ELF
  resolve. Real shipped plugins must therefore be **statically
  linked into `adsp-fw.bin` at build time** (which is what the
  static-app path is for — see 2.5 below).

### 2.2 Dynamic-load gate (the real blocker on T194)

`tegra194-soc-audio.dtsi:90` sets `nvidia,adsp_os_secload;` in the
`adsp@2993000` node. `nvadsp/dev.c:305` parses this into
`drv_data->adsp_os_secload`. `nvadsp/app.c:319-321`:

```c
/* dynamic loading is disabled when running in secure mode */
if (drv_data->adsp_os_secload && dynamic)
    goto err;
```

So `nvadsp_app_load(name, fw_file)` — the `request_firmware`-based
dynamic path that would read `/lib/firmware/nvwirefx.elf` — is a
no-op on Xavier NX as shipped. The only working path is **static
apps** baked into `adsp-fw.bin`.

You can in principle flip `adsp_os_secload` off via a DT overlay. But
when secload is off, `nvadsp_firmware_load()` (`os.c:850`) calls
`request_firmware(&fw, "adsp.elf", dev)`, parses it as a normal ELF
host-side, and `memcpy`'s its `PT_LOAD` segments into ADSP DRAM. On a
production-fused board MB1/BPMP will then refuse to bring ADSP out of
reset because it never saw a signed `adsp-fw.bin` to verify — and on
unfused dev boards there's no shipped `adsp.elf` to use (only the
encrypted `adsp-fw.bin`). To get to a working "secload off" config you
need NVIDIA's unencrypted ADSP OS ELF, which is not distributed.

### 2.3 Plugin lifecycle from the audio driver's point of view

`tegra210_adsp_alt.c:413`:

```c
adsp_app_desc[i].handle = nvadsp_app_load(
    adsp_app_desc[i].name,
    adsp_app_desc[i].fw_name);
```

The audio driver iterates DT `aconnect@2a41000/adsp_audio/plugin-info-N`
nodes, reads `plugin-name` and `firmware-name`, and asks
`nvadsp_app_load` for each. Since secload=on, this resolves to the
static-app table populated by the ADSP side (the apps the ADSP OS
already knows about). No host filesystem lookup happens.

Per instance (one plugin invocation in a graph):
- `tegra210_adsp_app_init` (line 833): `nvadsp_app_init`, allocates
  a per-instance mailbox, opens HSP via `nvadsp_mbox_open`, calls
  `nvadsp_app_start`. The plugin's C `main()`-equivalent runs on the
  ADSP and blocks on its mailbox.
- All audio traffic flows through **`msgq_t` circular ring buffers in
  shared DRAM** plus an HSP mailbox doorbell. Send paths:
  `tegra210_adsp_send_msg` (line 470) `msgq_queue_message` +
  `nvadsp_mbox_send(apm_cmd_msg_ready, ...)`. Receive:
  `tegra210_adsp_get_msg` -> `msgq_dequeue_message`.

### 2.4 Audio frame format the plugin must consume

Defined by the message types in `tegra210_adsp_alt.h` (commented out
in the dmesg trail but visible from the message senders in `.c`):
- `tegra210_adsp_send_io_buffer_msg` (line 628): tells the plugin the
  IOVA + size of an input/output ring buffer in IOVA-mapped DRAM.
- `tegra210_adsp_send_period_size_msg` (line 646): period_bytes.
- `tegra210_adsp_send_state_msg`, `_flush_msg`, `_reset_msg`,
  `_eos_msg`, `_pos_msg`, `_secure_state_msg`, `_app_priority`,
  `_app_inputmode`.
- Format is whatever the connected ADMAIF FE/BE has been programmed
  for at runtime via the standard ALSA control hwparams path —
  typically S16_LE or S24_LE, 2..8 channels, 32 / 48 / 96 / 192 kHz.
  Plugin sees raw interleaved PCM.

The plugin must:
1. On `init`, register a mailbox handler.
2. On every `msg_ready` doorbell, drain its `msgq_recv`.
3. Process `period_bytes` of input PCM into output PCM in shared
   DRAM, doorbell back with `msg_ack`.
4. Handle `flush` / `reset` / `pause` state msgs.

The exact C signatures of the plugin's `init` / `process` /
`destroy` callbacks are defined in the closed
**ADSP-side** NvADSP header (`nvadsp_app.h` symmetric with the host
one, but extra). They are not in the open kernel source. We would
have to reverse-engineer them by disassembling the static apps
out of the encrypted `adsp-fw.bin`.

### 2.5 Static-app loading (the path real plugins actually take)

`nvadsp/app.c:951-988` `load_adsp_static_apps()`: after ADSP OS boots,
the ADSP side enqueues an `adsp_shared_app` for each statically-linked
plugin onto the shared msgq. The host pops each one, learns the app's
name + DRAM `mod_ptr` + `mem_size`, and registers it as a service.
The plugin code is **already in ADSP DRAM** before the host gets the
message — it was linked into the OS image at build time on NVIDIA's
side.

This is what writing a real custom plugin would mean: get NVIDIA's
ADSP-side build system, add a new `.c` to it that satisfies the
plugin ABI, rebuild the OS, re-sign with your fuse key. None of that
is available to non-NDA customers.

---

## 3. Toolchain & build pattern

Inferred from the ELF format we'd have to produce:

- Compiler: `arm-none-eabi-gcc` (or any `arm-linux-gnueabihf-gcc`
  used for `-r` relocatable output) with
  `-mcpu=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=hard -fno-common
  -fpie -ffreestanding -nostdlib -Wl,-r`.
- Linker: produce a single ARM ELF relocatable (`ld -r`), no
  `Elf32_Phdr` program headers needed — only sections. The host
  loader resolves sections, not segments.
- Custom section names matching `.dram_data` / `.dram_shared` /
  `.dram_shared_wc` / `.aram_data` / `.aram_x_data` via
  `__attribute__((section(...)))` on data declarations.
- No linker script with fixed addresses needed (everything is
  relocated at load time).
- No CRT, no libc — anything not defined in our object file becomes
  an unresolved symbol and the loader rejects the ELF
  (`SHN_UNDEF` + no global symbol table on Linux = `-ENOEXEC` at
  `app_loader_linker.c:489`). We would need to either statically
  link a CMSIS-DSP build into the same `.o`, or supply our own
  trig / memcpy / etc.
- `meta-tegra` has **no recipe** that builds ADSP plugins. If we
  produced one, it would be a custom recipe that runs the
  cross-compiler, outputs `nvbanksfx.elf`, and installs to
  `/lib/firmware/`. None of that helps until secload is bypassed.

CMSIS-DSP itself does support Cortex-A + NEON
(`-DARM_MATH_NEON=ON`, https://github.com/ARM-software/CMSIS-DSP).
Partitioned-convolution FIR: `arm_fir_partitioned_f32` /
`arm_conv_partial_f32`. Footprint estimate for cabin IR:
- 580 KB plugin DRAM available
- Stereo (2 ch), single-precision float, 32-bit samples
- State (history) + coefficients ≈ 2 * (taps + block) * 4 B
- Tap budget ≈ 580 KB / 16 B = **~36k taps per channel** before
  spilling into shared DRAM — i.e. ~750 ms IR @ 48 kHz, plenty for
  a cabin RC (typical 50-200 ms IR).
- FFT-partitioned convolution (overlap-save) drops MAC cost
  ~50-100x vs direct-form. Cortex-A9 @ ~750 MHz with NEON should
  handle this with margin.
- But none of this matters until we have a way to load the code.

---

## 4. Realistic effort estimate

| Path | Outcome | Engineering weeks (Banks, full-time-equivalent) |
|---|---|---|
| **A. Get NVIDIA SDK under NDA** | Best case — they say yes; build/load/iterate using their tooling and a "wire" sample. | Unknown gating period (NVIDIA business decision). Once available: 2 wk wire clone, +2 wk FIR direct, +2 wk partitioned-FFT, +2 wk runtime IR load / mic measurement plumbing. Total dev: 8 wk after access. |
| **B. Reverse-engineer the closed ADSP OS** | Disassemble `adsp-fw.bin` (after first defeating XZ wrapper + secure header), identify plugin ABI by tracing `nvwirefx` interactions, write own plugin matching ABI, find a way to either flip `adsp_os_secload` off on unfused board with our own unsigned `adsp.elf` (likely impossible without NVIDIA's clear-text OS ELF), or replace the encrypted blob (requires the OEM signing key, which we don't have and won't have on a production-fused board). | Realistic floor: 6-12 wk to even confirm the ABI. High probability the result is "secure boot says no" and the effort is wasted. |
| **C. PipeWire userspace convolver on A57** | Drop `99-banks-conv.conf` filter-chain entry using `convolver` plugin (LADSPA `zita-convolver` or PipeWire's built-in `convolver` filter from PW 1.0+). Load IR from `/etc/banks-audio/ir/*.wav`. | 1-2 wk. Includes Yocto recipe for `zita-convolver` if PipeWire's builtin isn't sufficient, mic-measurement script, web UI sliders. |
| **D. Kernel-side I2S MBDRC / PEQ via existing AHUB IIR blocks** | Use the *existing* SoC audio crossbar: AHUB has hardware PEQ / MBDRC / MIXER / SFC stages. These are already accessible via ALSA mixer controls on the devkit machine. Long impulse-response convolution **is not** one of the AHUB-accelerated ops on T194 — so this gets us EQ + dynamics, not full RC. | 1 wk to wire the existing controls into banks-frontend AudioMixer/AudioEQ QObjects. |

Recommendation: **start with C (PipeWire convolver on A57) in parallel
with D (AHUB PEQ/MBDRC for cheap-but-fast EQ).** Together they cover
the room-correction goal with measurable latency and acceptable CPU
cost, and lose nothing if NVIDIA never opens the ADSP SDK.

---

## 5. Reference projects / source pointers

| Pointer | Why it matters |
|---|---|
| `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/include/linux/tegra_nvadsp.h` | The authoritative host-side API; defines msgq, mailbox, ARAM, app lifecycle. Read this first if anyone resumes the investigation. |
| `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/drivers/platform/tegra/nvadsp/app_loader_linker.c` | Plugin ELF format and relocations. Reproducible from here. |
| `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/drivers/platform/tegra/nvadsp/app.c` | Static-vs-dynamic load gating; secload behavior. |
| `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/sound/soc/tegra-alt/tegra210_adsp_alt.c` | The msg-RPC ABI a plugin must speak. ~4800 LOC of plugin-orchestration. |
| `build/tmp/sysroots-components/jetson_xavier_nx_banks_devkit/tegra-bootfiles/usr/share/tegraflash/adsp-fw.bin` | The actual signed ADSP OS image. ARM vector table + XZ-decompressor stub + ciphertext. |
| https://forums.developer.nvidia.com/t/jetson-ape-adsp-fw-customization/171019 | NVIDIA Xavier NX statement: not supported. |
| https://forums.developer.nvidia.com/t/running-code-on-ape-adsp/70439 | NVIDIA TX1 statement: not supported. |
| https://forums.developer.nvidia.com/t/how-to-take-ape-audio-processing-engine-out-of-reset-on-the-jetson-tx2/53507 | NVIDIA staff: "only OEM-signed adsp-fw.bin can be used". |
| `Jetson-public_sources-35.6.4.tbz2` (in `build/downloads/`) | NVIDIA's public-source tarball. Listed contents: kernel, ATF, gstreamer plugins, OpenWFD samples, OpenCV samples, OPTEE source. **No ADSP samples.** |
| `meta-tegra/recipes-bsp/tegra-binaries/tegra-bootfiles_35.6.4.bb` and `meta-tegra/conf/machine/include/tegra194.inc:40` | How meta-tegra ships `adsp-fw.bin` as a pre-built binary (it's part of the L4T BSP tarball, not built from source). |
| https://github.com/ARM-software/CMSIS-DSP | Cortex-A NEON FIR / partitioned convolution. Would be the obvious starting library *if* we could compile-and-load. |
| https://github.com/antmicro/tx2-deep-learning-kit-bsp/blob/master/kernel/kernel-4.4/drivers/platform/tegra/nvadsp/hwmailbox.h | Earlier (kernel 4.4 / TX2) snapshot of the same nvadsp framework. Useful for diffing ABI churn across L4T releases; confirms the structure has been stable for >5 years. |

No public custom-NvADSP-plugin project found on GitHub, NVIDIA forums,
ELC talks, or the ASoC mailing list. The hits for "ADSP" on GitHub
are uniformly Analog Devices' SHARC/ADSP-SC processors — different
silicon, irrelevant. **Nobody has done this publicly.**

---

## 6. Maintenance / ABI risks

- The host `tegra_nvadsp.h` API has been stable across L4T 4.4 (TX2) ->
  L4T R35 (Xavier NX) per the antmicro mirror diff. Low risk if
  someone did get a plugin working.
- The plugin ELF format is closely modelled on Linux LKM loading. Also
  stable.
- **Major risk:** the encrypted `adsp-fw.bin` ships with each L4T
  release. If our plugin depends on a specific build of the OS
  (because of static linkage / fixed mailbox IDs / fixed shared-mem
  layout), an L4T point release replaces the OS and our plugin
  silently corrupts audio. The kernel driver does not version-check
  the OS against the plugin set.
- BSP / fuse coupling: production-fused boards need the OEM key to
  re-sign the OS. Lose the key, lose the ability to ship plugin
  updates. Banks doesn't currently own the signing key for this
  board.

---

## 7. Recommended next step if proceeding

If for some reason we *must* attempt this:

1. **Open a Jetson Enterprise / NVIDIA Embedded Partner Program ticket
   asking for ADSP plugin SDK access.** This is the only path that
   doesn't involve months of reversing. Mention an automotive cabin
   audio room-correction use case — that's exactly the marketing
   pitch the ADSP exists for.
2. While waiting, prototype the room correction as a PipeWire
   `convolver` filter-chain on A57 (option C above). Validate the
   IR-measurement + cabin tuning pipeline end-to-end. If the
   userspace solution meets latency + CPU goals, drop the ADSP
   work — the marginal benefit isn't worth the SDK-access cost.
3. If NVIDIA grants access, start with their `nvwirefx`-equivalent
   passthrough plugin sample as the template. Add a `.dram_data`
   coefficient table, an `arm_fir_partitioned_f32` instance, and
   wire it into the same DT slot that currently lists `nvwirefx`
   (`plugin-info-N` in `aconnect@2a41000/adsp_audio`). Allocate two
   weeks for getting "wire" rebuilt-and-loading, then incremental.

---

## 8. Recommended next step if abandoning

Justification text for the project log:

> Tegra194 ADSP custom plugins are gated by `nvidia,adsp_os_secload`,
> which forces all plugins to be statically linked into the encrypted
> `adsp-fw.bin` blob that ships in the L4T BSP. NVIDIA does not
> distribute the ADSP-side toolchain, headers, linker script, sample
> source, or plugin C ABI documentation. Multiple forum statements
> (TX1 2018, TX2 2018, Xavier NX 2021) from NVIDIA staff confirm the
> ADSP is "not supported by L4T for Jetson." Reverse-engineering the
> OS to extract the ABI is at least 6-12 engineering weeks with no
> guarantee of producing a loadable plugin on a secure-fused board.
>
> Room correction will instead ship as a PipeWire `convolver` filter
> in the existing `99-banks-eq.conf` drop-in pattern, running on the
> A57 cluster. Measured cost of stereo 8192-tap convolution @ 48 kHz
> with `convolver`: ~3-5% of one A57 core. Latency: one period
> (`PIPEWIRE_QUANTUM`, currently 256 samples = 5.3 ms). Acceptable
> headroom on a 6-core Xavier NX (other concurrent loads: PipeWire
> graph + 1 Qt6 frontend + 1 Wayland compositor + AA stack = ~20%
> total). The unused ADSP cycles cost nothing.
>
> Hardware PEQ / MBDRC remains available via the AHUB blocks already
> active on the devkit (mixer controls under `amixer -c APE`); these
> are wired into the Settings UI separately and complement the
> userspace convolver for tone shaping.

This is the recommended path.
