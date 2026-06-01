# ADSP Plugin Liveness — Xavier NX devkit (L4T R35.6.4)

Date: 2026-06-01
Target: `root@192.168.55.1` (`jetson-xavier-nx-banks-devkit`, kernel `5.10.216-l4t-r35.6.4`)
ADSP OS version: `0.4.0 [Mon Jan 26 23:27:37 PST]` (from `/sys/kernel/debug/tegra_ape/adspos_version`)
ADSP OS load mode: **secload** (`/proc/device-tree/aconnect@2a41000/adsp@2993000/nvidia,adsp_os_secload` present)

## TL;DR — only 1 of 6 plugins is actually usable

Prior investigation claimed all 6 audio plugins (mp3-dec1, spkprot, src, aac-dec1, aec, wire) are statically linked into the encrypted `adsp-fw.bin` and therefore available. **That is wrong on our image.** Live test: only **WIRE** initializes. The other 5 fail with `Failed to init app <name>(<elf>.elf)`. Root cause is `nvidia,adsp_os_secload=1` + the encrypted OS image only exposes a subset of shared-app entries; the 5 missing plugin ELFs would have to come from rootfs `request_firmware`, and dynamic ELF loading is **explicitly blocked under secload** (see `nvadsp/app.c:320`).

## Per-plugin liveness verdict

| Plugin | Widget | Firmware ELF | DT slot | Init test | Verdict |
|---|---|---|---|---|---|
| wire | `WIRE` | `libnvwirefx.elf` | plugin-info-6 | `apm init app wire done` after `WIRE MUX = ADSP-FE1` | **LIVE** |
| spkprot | `SPKPROT-SW` | `nvspkprot.elf` | plugin-info-2 | `Failed to init app spkprot(nvspkprot.elf)` | **NOT-LIVE** |
| mp3-dec1 | `MP3-DEC1` | `nvmp3dec.elf` | plugin-info-1 | `Failed to init app mp3-dec1(nvmp3dec.elf)` | **NOT-LIVE** |
| aac-dec1 | `AAC-DEC1` | `nvaacdec.elf` | plugin-info-4 | `Failed to init app aac-dec1(nvaacdec.elf)` | **NOT-LIVE** |
| aec | `AEC` | `nvoice.elf` | plugin-info-5 | `Failed to init app aec(nvoice.elf)` | **NOT-LIVE** |
| src | `SRC` | `nvsrc.elf` | plugin-info-3 | `Failed to init app src(nvsrc.elf)` | **NOT-LIVE** |

### Evidence (dmesg)

```
[    7.490866] tegra210-adsp tegra210-adsp: Loaded app wire
[    7.490877] tegra210-adsp tegra210-adsp: Loaded app apm
[    7.490883] tegra210-adsp tegra210-adsp: Loaded app adma
[    7.490889] tegra210-adsp tegra210-adsp: Loaded app adma_tx
...
[ 2507.616422] tegra210-adsp tegra210-adsp: apm init app wire done            # MUX -> ADSP-FE1
[ 2519.495727] tegra210-adsp tegra210-adsp: Failed to init app spkprot(nvspkprot.elf)
[ 2519.584251] tegra210-adsp tegra210-adsp: Failed to init app mp3-dec1(nvmp3dec.elf)
[ 2519.671241] tegra210-adsp tegra210-adsp: Failed to init app aec(nvoice.elf)
[ 2519.759939] tegra210-adsp tegra210-adsp: Failed to init app aac-dec1(nvaacdec.elf)
[ 2519.850261] tegra210-adsp tegra210-adsp: Failed to init app src(nvsrc.elf)
```

### debugfs apps actually loaded into ADSP DRAM

`/sys/kernel/debug/tegra_ape/adsp_apps/` lists 11 apps (with load addresses) — these are the **shared apps** the secure OS exposes:

```
adma_test, adsp_lpthread, adsp_submit, adspff,
csm_sm, libnvwirefx, mods_app, nvadma, nvadma_tx, nvapm
```

Only `libnvwirefx` is an "audio plugin." The other 10 are framework helpers (lifecycle, FF/RW, console submit, CSM, adma channels, APM = audio-processing-module supervisor). `apm/adma/adma_tx/wire` are the 4 that `tegra210_adsp_alt.c::tegra210_adsp_init()` (line 412–419) successfully wired into ASoC; the 5 plugins above are in the same boot loop but `nvadsp_app_load()` returned NULL for them so their dev_info "Loaded app" never fired.

### Why the 5 plugins fail — code path

`nvidia/drivers/platform/tegra/nvadsp/app.c:317-321`:

```c
if (!ser) {
    /* dynamic loading is disabled when running in secure mode */
    if (drv_data->adsp_os_secload && dynamic)
        goto err;
```

`nvadsp_app_load("spkprot", "nvspkprot.elf")` enters `app_load(..., dynamic=true)`. Since `spkprot` is not in the shared-app list reported by the secure ADSP OS at boot, `get_loaded_service()` returns NULL, the secload check fires, `goto err` → handle NULL → dmesg gets `Failed to init app ...`. Same path for all 4 other "missing" plugins.

Practical conclusion: even though the DT advertises 6 plugin slots and the kernel driver wires up 6 ALSA MUXes for them, **only the plugins whose code blob is bundled into `adsp-fw.bin`'s static-app table will work**. Our image's `adsp-fw.bin` only ships `wire` (and the framework apps).

## adsp-fw.bin packaging audit

### Source / sysroot location

- `meta-tegra/recipes-bsp/tegra-bootfiles/` produces `tegra-bootfiles-35.6.4` which stages the binary to:
  - `build/tmp/sysroots-components/jetson_xavier_nx_banks_devkit/tegra-bootfiles/usr/share/tegraflash/adsp-fw.bin`
  - md5 `27b2ec34763297700bcbd4ab557acab2`, size 392 732 bytes
- Same identical blob also under `work/.../recipe-sysroot/usr/share/tegraflash/adsp-fw.bin` and `work-shared/L4T-tegra-35.6.4-r0/Linux_for_Tegra/bootloader/adsp-fw.bin` (the NVIDIA `Linux_for_Tegra` extraction).
- Listed in `meta-tegra/conf/machine/include/tegra194.inc:40` under `TEGRA_BOOT_FIRMWARE_FILES` — that variable drives `tegra-bootfiles` to copy it from the `Linux_for_Tegra/bootloader/` upstream pack into the BSP sysroot.

### Flash-time partition wiring

`meta-tegra/recipes-bsp/tegra-binaries/tegra-helper-scripts/tegra-flash-helper.sh:685`:

```
ape_fw adsp-fw.bin; \
```

This token is consumed by `tegraflash.py` / the t194 flashing flow as a `BINSARGS` entry. The binary is signed at flash time (with the SBK or null key, depending on fuses) and written to the `adsp-fw` partition. **Not staged into rootfs `/lib/firmware/`** — confirmed on target: `find / -name 'adsp-fw*' 2>/dev/null` returns only `/dev/disk/by-partlabel/adsp-fw_b`. There is no `/lib/firmware/tegra19x/adsp-fw.bin`.

### Active partition

Target has only `adsp-fw_b` labelled (no `_a` rotating slot configured); active block device is `/dev/mmcblk0p4`, partition size 1 MiB. The first 392 732 bytes of that partition do not byte-match the staged build artifact (`06441bf157fa33948c5e8d1354b99e5a` vs `27b2ec34763297700bcbd4ab557acab2`) — that mismatch is expected, the partition contains the **signed wrapper** (`*.bin.signed` equivalent emitted by `tegraflash.py` during flash), not the bare source. The bare-file md5 across all staging copies in our build tree is identical (`27b2ec3...`), so what we ship to flash is consistent and one version only.

### Variants

- `tegra194.inc:40` (us) and `tegra234.inc:36` (Orin) both list `adsp-fw.bin`. Same filename, different binaries — meta-tegra's `Linux_for_Tegra/bootloader/` pack ships one per SoC, the one in our build is the t194 build.
- No debug/release split in the BSP pack: single `adsp-fw.bin` per SoC family.

## Per-plugin ALSA control surface

For each plugin, three ALSA mixer controls exist regardless of liveness:

- `<WIDGET> MUX` (132-item enum) — selects ADSP-side input (ADSP-FE / ADSP-ADMAIF / NULL-SINK / APM IN+OUT / ADMA / other plugin). Setting MUX to non-None on the 5 NOT-LIVE plugins triggers the init failure shown above.
- `<WIDGET> set params` (bytes type) — opaque parameter blob that the running ADSP plugin consumes. Layout is plugin-defined (NVIDIA-private). The kernel just forwards `tegra210_adsp_set_param` payload to the plugin via shared mem.
- `<WIDGET> send bytes` (bytes type) — opaque payload for runtime commands. Same plumbing as set params, different opcode tag.

There are **no individual per-knob controls** (no e.g. `SPKPROT thermal_limit`, no `AEC ref_input_gain`). All tuning happens by writing a NVIDIA-proprietary TLV blob into `set params`. The structure of that blob is documented only in NVIDIA's `nvaudio` userspace headers (`tegra_audio_plugin_manager.h` etc.), which are not in our image and are shipped only as part of the `nvaudio` deb in the closed L4T BSP package (not in meta-tegra). Without those headers the controls are effectively useless from open userspace.

The only AHUB-side control that is **not** an ADSP plugin and that we can use today is `SPKPROT1 Mux` (the AHUB hardware speaker-protection block input selector, 81-item enum) — but that's the AHUB MIXER/SFC fabric piece, not the `nvspkprot.elf` ADSP plugin. It is just a routing knob, not a DSP processor.

## Recommended use cases

Given only WIRE is live:

1. **WIRE plugin as a measurement / passthrough probe.** Routing `ADSP-FE1 → WIRE → ADSP-ADMAIF1` is the simplest way to confirm the ADSP audio data path is up. Lossless, no parameter set. Useful as a sanity check or for capturing ADSP-domain audio without inserting processing.
2. **Do NOT design any feature around mp3-dec1 / aac-dec1 / spkprot / aec / src.** These will not run unless we either:
   - Switch ADSP OS load mode to **non-secload** AND obtain plain `.elf` builds of each plugin from NVIDIA (not shipped in meta-tegra; would need to extract from `nvaudio` BSP deb and place into `/lib/firmware/tegra19x/`). Even then, several of them (AAC/MP3) likely have license/royalty implications.
   - Or get a different `adsp-fw.bin` that bundles them as shared apps. NVIDIA does not publish such a variant for R35.6.4.
3. **All practical DSP processing should stay on CPU (PipeWire `param_eq` / `filter-chain`).** The AHUB MIXER/SFC fabric (MVC1/OPE1/MIXER1/SFC1) is available and useful for routing/volume/mixing, but is not a flexible DSP — it offers volume + a fixed 6-band PEQ inside OPE1 only.
4. **Speaker protection on this platform** is realistically a userspace PipeWire filter — not the ADSP `spkprot` plugin. Same for AEC (use libwebrtc-audio-processing in PipeWire). The on-die DSP path is not reachable here.

## Things checked & set back to baseline

- Toggled each plugin MUX to `ADSP-FE1` to provoke init, then back to `None`. Final state verified: all 6 plugin MUXes report `values=0` (None).
- No production routing was active during the test (no `banks-audio-route` script on this target — appears not yet deployed; the prior `ADMAIF1 -> MVC1 -> OPE1 -> I2S5` chain was not touched, and SPKPROT1 / SFC / OPE / I2S5 muxes were not modified).

## References (build tree)

- `meta-tegra/conf/machine/include/tegra194.inc` — `TEGRA_BOOT_FIRMWARE_FILES` declaration
- `meta-tegra/recipes-bsp/tegra-binaries/tegra-helper-scripts/tegra-flash-helper.sh:685` — `ape_fw adsp-fw.bin` flash-arg
- `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/sound/soc/tegra-alt/tegra210_adsp_alt.c:395-439` — app load loop
- `build/tmp/work-shared/jetson-xavier-nx-banks-devkit/kernel-source/nvidia/drivers/platform/tegra/nvadsp/app.c:305-355` — `app_load()` with secload gate at line 320
- `/proc/device-tree/aconnect@2a41000/adsp_audio/plugin-info-{1..6}/` — DT plugin slot declarations
- `/proc/device-tree/aconnect@2a41000/adsp@2993000/nvidia,adsp_os_secload` — secload flag present
