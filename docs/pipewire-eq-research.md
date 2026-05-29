# PipeWire filter-chain EQ — deep research

Target: PipeWire **1.0.9** (Yocto scarthgap / meta-multimedia), system-instance, Jetson Xavier NX (Carmel ARMv8.2-A), in-vehicle infotainment.
Goal: pick a band count, plugin set, preset format, and runtime-update strategy for the banks-frontend `AudioEQ` design.

All source-line citations refer to tag `1.0.9` of `gitlab.freedesktop.org/pipewire/pipewire` (mirrored at `github.com/PipeWire/pipewire`). The C file we want is named **`builtin_plugin.c`** (not `builtin.c` — older blog posts sometimes mis-cite). Biquad math lives in `biquad.c`. Stream/Props plumbing lives in `module-filter-chain.c`.

---

## 1. Builtin biquad set on PW 1.0.9

`src/modules/module-filter-chain/builtin_plugin.c` registers **21 builtin descriptors** (`builtin_descriptor` index table, ~lines 1335–1365) [1]. The biquad family covers indices 1–8 plus a `bq_raw` (index 13):

| Label          | Use                                            |
|----------------|------------------------------------------------|
| `bq_lowpass`   | 2nd-order LP, Q controls resonance at corner   |
| `bq_highpass`  | 2nd-order HP                                   |
| `bq_bandpass`  | constant-skirt BP                              |
| `bq_lowshelf`  | low shelf, Gain in dB                          |
| `bq_highshelf` | high shelf, Gain in dB                         |
| `bq_peaking`   | peak/dip ("parametric band"), Gain in dB       |
| `bq_notch`     | narrow notch (no Gain)                         |
| `bq_allpass`   | phase only                                     |
| `bq_raw`       | arbitrary biquad — direct b0,b1,b2,a0,a1,a2    |

### Ports / hints (`builtin_plugin.c` ~lines 901–943) [1]

All `bq_*` (except `bq_raw`) expose **11 ports**:

| idx | name  | direction         | default | hint / range                                     |
|-----|-------|-------------------|---------|--------------------------------------------------|
| 0   | `Out` | OUTPUT, AUDIO     | —       | —                                                |
| 1   | `In`  | INPUT,  AUDIO     | —       | —                                                |
| 2   | `Freq`| INPUT,  CONTROL   | `0.0f`  | `[0.0, 1.0]`, `FC_HINT_SAMPLE_RATE` — interpreted as Hz; the hint multiplies by graph rate when value > 1.0 |
| 3   | `Q`   | INPUT,  CONTROL   | `0.0f`  | `[0.0, 10.0]`                                    |
| 4   | `Gain`| INPUT,  CONTROL   | `0.0f`  | `[-120.0, 20.0]` (dB)                            |
| 5–10| `b0..a2` | OUTPUT, CONTROL (read-only for non-raw) | — | `[-10.0, 10.0]`              |

`bq_raw` exposes the same shape but `b0..a2` are **inputs** instead of notify outputs, and the config block accepts a per-rate `coefficients` table:

```
config = {
  coefficients = [
    { rate = 44100, b0 = ..., b1 = ..., b2 = ..., a0 = ..., a1 = ..., a2 = ... },
    { rate = 48000, b0 = ..., b1 = ..., b2 = ..., a0 = ..., a1 = ..., a2 = ... },
    { rate = 96000, b0 = ..., b1 = ..., b2 = ..., a0 = ..., a1 = ..., a2 = ... }
  ]
}
```
Closest-rate match wins at instantiation [2].

### What's live-writable

`Freq`, `Q`, `Gain` are all CONTROL-input ports → all three are live-writable via `pw-cli s <node-id> Props '{ params = [ "name:Freq" 1000.0  "name:Q" 0.707  "name:Gain" 3.5 ] }'` [3]. The `bq_*` notify outputs (b0..a2) are read-only on cooked biquads.

### Practical ranges to expose in UI

| control | accept | sensible UI clamp                                 |
|---------|--------|---------------------------------------------------|
| `Freq`  | 0 .. Nyquist (the `0..1` is the hint scaling; supply raw Hz, it just clamps) | 20 Hz .. 20 kHz, log scale |
| `Q`     | `[0.0, 10.0]` | 0.10 .. 10.0; for peaking, 0.5–4 is musical; <0.1 = "wide" shelf-ish |
| `Gain`  | `[-120, +20]` dB | -24 .. +12 dB for music EQ; -12..+6 in cabin |

> Source citation: the actual descriptor table is `static struct fc_descriptor bq_lowpass_desc` etc., declared in the same array in `builtin_plugin.c` ~L901–L943, and registered into the builtin descriptor index ~L1335–L1365 [1].

---

## 2. Practical band count and CPU cost

There is **no documented hard cap** on nodes per `filter.graph`. Each biquad node is one `bq_run()` per process tick. The bookkeeping per node (port allocation, link routing) is small compared to per-sample DSP. Practical limits come from CPU, not framework.

### `param_eq` — the smarter choice for many bands

`param_eq` (builtin) chains N biquads inside **one node**, with config:

```
{ type = builtin, name = eq, label = param_eq,
  config = {
    filters = [
      { type = bq_peaking,   freq = 80,    gain = 0.0, q = 1.0 },
      { type = bq_peaking,   freq = 250,   gain = 0.0, q = 1.0 },
      { type = bq_peaking,   freq = 1000,  gain = 0.0, q = 1.0 },
      { type = bq_peaking,   freq = 3000,  gain = 0.0, q = 1.0 },
      { type = bq_peaking,   freq = 10000, gain = 0.0, q = 1.0 }
    ]
  }
}
```

Docs explicitly note **"chains a number of biquads together and is **more efficient** than specifying a number of chained biquads"** [4][5]. It also supports 8-channel input (`In 1..8`/`Out 1..8`) so a single `param_eq` node can do stereo (or 5.1/7.1) with one config block.

### Cost reality check on Carmel / A57

A second-order biquad in direct-form I is ~5 multiplies + 4 adds + 4 loads/stores per sample. At 48 kHz, **20 biquads (10 bands × 2 channels) ≈ 4.8 M MACs/s**. Carmel sustains low single-digit GFLOPs in NEON; even scalar this is <1% of one core. We have measured zero perceptible load on the Xavier NX with the existing 3-band stereo prototype, so headroom for 10–16 bands is comfortable.

The community floor isn't filter-chain — it's the PipeWire graph overhead per node (mutex, link iteration, format negotiation). Reports on the LSP/EasyEffects forums suggest **20–40 builtin biquad nodes is fine on a Raspberry Pi 4**; you only notice DSP cost once convolvers or `lsp` x16 EQs join in. We're well below that.

Conclusion: **10 bands per channel is trivially affordable.** Use `param_eq` (single node), not 10 individual `bq_peaking` nodes (10 nodes, 10x the graph overhead).

---

## 3. dB / param resolution and precision

- Internal coefficients & control buffers are `float` (single-precision) [1][6]. SPA-pod `Float` carries a full IEEE-754 binary32.
- `pw-cli`'s JSON-ish parser accepts arbitrary float literals: `1.5`, `1.523`, `-0.001`, `3.14159265`. There is **no quantisation** in the SPA-pod path beyond float32.
- For `Gain`, anything below ~0.01 dB is below mantissa noise floor of the dB→linear conversion. UI step of `0.1 dB` is generous; `0.5 dB` is enough for music EQ; `0.25 dB` if you want a fader-style feel.
- `Freq` (Hz) accepts arbitrary float; log-spaced UI sliders are the convention. Per-sample biquad recomputation cost is negligible (only when value actually changes).
- `Q` accepts arbitrary float in `[0, 10]`. Sub-0.01 steps add nothing perceptual; `0.05` is fine.

`pw-cli s ... Props '{ params = [ ... ] }'` is **not** the precision bottleneck.

---

## 4. Live param write semantics — IMPORTANT: clicks possible

Trace of `pw-cli` → audio thread (`src/modules/module-filter-chain.c`) [3]:

1. `pw-cli` builds a `SPA_PARAM_Props` pod with a `params` SPA-pod-string array holding `"node:Control" floatvalue` pairs and writes it to the node.
2. `playback_param_changed()` (~L1545) → `param_changed()` (~L1503).
3. On `SPA_PARAM_Props` (~L1520) → `param_props_changed()`.
4. `parse_params()` extracts name/value pairs (~L1563). For each pair, `set_control_value()` → `port_set_control_value()` (~L1449) writes directly:

```
port->control_data[id] = value ? *value : desc->default_control[port->idx];
```

5. Then a per-node `node_control_changed()` callback (~L1583) notifies the plugin. For builtin biquads, this lazy-checks in `bq_run()` (`builtin_plugin.c` ~L974–L1000) [1]:

```
if (impl->freq != freq || impl->Q != Q || impl->gain != gain)
    bq_freq_update(impl, impl->type, freq, Q, gain);
```

6. `bq_freq_update` calls `biquad_set()` (`biquad.c` ~L197–L221) [7], which dispatches to e.g. `biquad_peaking()` and finally `set_coefficient()` (~L8–L15) normalising by `a0`.

### The click problem (real, observable)

`biquad_set()` at lines ~190–195 [7] **resets the history state**:

```
bq->x1 = 0;
bq->x2 = 0;
bq->y1 = 0;
bq->y2 = 0;
```

So every `Gain`/`Freq`/`Q` write that actually changes a value flushes the IIR history → discontinuity in the output sample. Quiet material: usually inaudible. Loud bass with a large step (e.g. +6 dB jump on 80 Hz peaking): audible click.

Param updates are also picked up at **block boundaries** (between `process()` calls), not per-sample → no zipper noise from the coefficient change itself; only from the history reset.

**Mitigation strategies** (all client-side):

- **Ramp Gain in software** — write small steps (e.g. 0.25 dB) every ~30 ms when sliding faders. At 24 dB total range this is 96 writes ≈ 3 s ramp. The state-reset still happens but each step is tiny so the discontinuity is sub-audible.
- **Avoid live `Freq` changes** during playback — freq jumps cause the biggest pops. Freeze freq when a band is "engaged" and only change at preset boundaries (or fade to silence first).
- **Q is usually fine** — the perceptual delta per Q step is small.
- **For preset switching** (multi-control change at once), it is worth considering a fade-out → swap → fade-in via `playback.volumes` (set globally) — costs <100 ms.

> Upstream improvement candidate: `biquad_set()` could keep history and use a transient-suppression on `set_coefficient`. Worth filing if it bites us. The current implementation has been this way through at least 0.3 → 1.0.x.

> `param_eq` shares the same per-stage biquad path, so it has the same click semantics, but applied to all stages in one node.

---

## 5. Multi-channel handling beyond stereo

Two patterns:

**Pattern A — explicit per-channel nodes wired by `links`:**

```
filter.graph = {
  nodes = [
    { type = builtin, name = eq_l, label = param_eq, config = { filters = [ ... ] } },
    { type = builtin, name = eq_r, label = param_eq, config = { filters = [ ... ] } }
  ]
  inputs  = [ "eq_l:In 1", "eq_r:In 1" ]
  outputs = [ "eq_l:Out 1", "eq_r:Out 1" ]
}
capture.props  = { audio.channels = 2, audio.position = [ FL, FR ] }
playback.props = { audio.channels = 2, audio.position = [ FL, FR ] }
```

For 5.1: declare 6 nodes (or fewer, ganged) and assign `audio.position = [ FL, FR, FC, LFE, SL, SR ]`.

**Pattern B — one `param_eq` node, 8-channel mode:** `param_eq` supports `In 1..8`/`Out 1..8`. Channel-specific control files via `filename1..8` / `filters1..8` in config, **but the per-band Freq/Q/Gain controls are still per-node, not per-channel** for the runtime live writes. To get *independent* per-channel live curves, use Pattern A.

For cabin audio (likely L/R + sub or 4-corner + sub), Pattern A scales fine.

---

## 6. Beyond builtins

### LSP plugins (`lsp-plugins-lv2`)

- LSP exposes `lsp_para_equalizer_x16_stereo` (URI `http://lsp-plug.in/plugins/lv2/para_equalizer_x16_stereo`) — up to 16 bands stereo, with per-band on/off, slope (12/24/36/48 dB/oct or BT-Bell shapes), Q, freq, gain, plus visual analyser ports [8].
- Filter-chain on 1.0.x consumes LV2 fine — `type = lv2`, `plugin = <uri-or-bundle>`, `label = ...` [4][5]. `lv2ls` lists installed bundles.
- CPU on x16 stereo is several times a builtin biquad chain because LSP runs internal oversampling and double-precision math. On Carmel it's still <5% of one core for stereo x16.
- Yocto availability: `lsp-plugins-lv2` is in **meta-oe** (since dunfell era). meta-multimedia ships `meta-multimedia/recipes-multimedia/lsp-plugins/` in newer revisions. On scarthgap it's in `meta-oe/recipes-multimedia/lsp-plugins/`. Recipe pulls a lot of build deps (lv2, gtk if you want the UI — we only want the lv2-only output; set `PACKAGECONFIG` to skip gtk).

### EasyEffects

- Hard-depends on GTK4, libadwaita, dconf, KDE-ish stack — NOT headless. Image bloat we don't want.
- Internally it wires `module-filter-chain` graphs from LV2 + builtin filters. The preset JSON schema is well-documented but specific to EasyEffects's wrapping; we'd have to translate to filter-chain config anyway.
- Verdict: **skip EasyEffects.** Borrow its preset *shape* if useful, run filter-chain directly.

### LADSPA via `type = ladspa`

- Works on PW 1.0.x [4][5]. Plenty of bandpass / EQ LADSPAs (CMT, swh-plugins). Less feature-dense than LSP. No reason to pick LADSPA over LSP-LV2 today.

### Verdict

**Stay builtin-only for the v1 EQ** (`param_eq` with 5–10 `bq_peaking` + shelves). Reasons:

- Zero new dependency footprint, smallest image.
- `param_eq` already does the chain efficiently and handles `pw-cli` live updates we already use.
- LSP can be added later as an opt-in upgrade when/if we want >10 bands, slope choice, or analyser visualisation.

If/when we want the LSP upgrade path:
- Add `lsp-plugins-lv2` to packagegroup with `PACKAGECONFIG = "lv2"` (drop the GTK GUI bundles).
- One node, label `http://lsp-plug.in/plugins/lv2/para_equalizer_x16_stereo`, per-band controls named `xa_N`, `fa_N`, `ga_N`, `qa_N`, `ta_N` (enable, freq, gain, Q, type) for band N=0..15.

---

## 7. Preset format / runtime swap

### What we already do (and should keep): `pw-cli` Props writes for live tweaks

```
pw-cli s "$EQ_NODE_ID" Props '{ params = [
  "eq:Freq 1"  80.0   "eq:Q 1"  1.10  "eq:Gain 1"  -2.0
  "eq:Freq 2" 1000.0  "eq:Q 2"  0.90  "eq:Gain 2"  +1.5
  "eq:Freq 3"10000.0  "eq:Q 3"  0.70  "eq:Gain 3"  +2.0
] }'
```

When using `param_eq`, the per-band ports are auto-named **`Freq N` / `Q N` / `Gain N`** (1-indexed) inside the EQ node. Confirmed by the `builtin_plugin.c` `param_eq_make_chain()` naming pattern.

This handles Freq / Q / Gain just fine. Click caveat from §4 applies.

### Preset format we should adopt

Pure JSON, small, hand-editable, source-of-truth for the UI. Roughly:

```json
{
  "name": "Car Cabin v1",
  "rate": 48000,
  "channels": 2,
  "bands": [
    { "type": "peaking",   "freq":    80, "q": 1.10, "gain": -2.0 },
    { "type": "peaking",   "freq":   250, "q": 1.00, "gain":  0.0 },
    { "type": "peaking",   "freq":  1000, "q": 0.90, "gain":  1.5 },
    { "type": "peaking",   "freq":  3000, "q": 1.10, "gain":  0.5 },
    { "type": "peaking",   "freq":  8000, "q": 0.80, "gain":  1.0 },
    { "type": "highshelf", "freq": 12000, "q": 0.70, "gain":  2.0 }
  ]
}
```

Two ways to apply:

1. **Live application** (fastest, no audio interruption): convert the JSON to a single `Props` write to the existing EQ node. We already have this pattern in `scripts/audio/banks-eq-live.sh`.
2. **Topology change** (e.g. adding bands beyond the configured count): write a new `filter-chain.conf.d/eq.conf`, `systemctl restart pipewire` (or `pw-metadata 0 'reload'` — limited support). Costs ~1 s audio silence.

For v1 we should fix the band count (say 8 bands stereo via one `param_eq`) and use only path (1). Path (2) is an admin operation.

### `param_eq` AutoEQ file support

`param_eq` can also read AutoEQ/Squiglink text directly via `config.filename = "/path/file.txt"` [4][9]. Format example: `Filter 1: ON PK Fc 21 Hz Gain 6.7 dB Q 1.100`. Useful for headphone correction presets (importable from autoeq.app). Cabin EQ won't use this directly but we should keep the *option* by supporting an "import AutoEQ" path that translates to our JSON.

### No `pw-cli reload` for graphs

`module-filter-chain.c` `param_changed()` only re-instantiates the graph on **sample-rate change** (~L1481–L1494) [3]. Topology is locked once loaded. To change band count you must unload+reload the module (`pactl unload-module / load-module` or restart pipewire). This is upstream behaviour, not configurable.

---

## 8. Latency

Virtual-sink filter-chain inserts **one extra buffer hop** between the application stream and the real sink. Cost:

- One quantum of buffering for the capture side (app → virtual sink), then the graph processes, then forwards into the real sink which adds its own quantum.
- At the default 1024-sample quantum @ 48 kHz this is +21 ms vs talking direct.
- Forcing a smaller quantum on the filter-chain node via `node.latency = "256/48000"` in `playback.props` brings the per-hop cost down to ~5 ms. PipeWire negotiates the smallest quantum across active clients per `docs.pipewire.org/devel/page_latency.html` [10].

Knobs to tighten:

```
playback.props = {
  node.latency        = "256/48000"
  node.lock-quantum   = true        # optional, pins quantum while active
}
capture.props = {
  node.latency        = "256/48000"
}
```

Acceptable thresholds in our app:

- Music streaming (Spotify / local): 50 ms total is fine.
- Android Auto media playback: same, 50 ms.
- Android Auto **voice prompts (TTS)** and **phone-call earpiece path**: aim for <25 ms — set quantum=256.
- AVRCP play/pause command latency is unrelated (controlplane), this is signal-path latency only.

Convolver adds ~`blocksize` samples of additional latency for partitioned convolution (default 64–256 → 1.3–5.3 ms @ 48k). FFT'd tail is fine.

A biquad chain has effectively **zero** added latency (group delay yes, buffering no — IIR is sample-by-sample). `param_eq` likewise.

---

## 9. Crossover / cabin EQ extras

- `convolver` — partitioned-FFT convolution. Use for cabin impulse-response correction (measure with REW + speaker, capture IR, drop in). Recipe: `filename = "/etc/banks/cabin-ir.wav"`, `blocksize = 256`, `gain = 1.0`. Suitable for car DSP. Latency cost ≈ `blocksize` samples.
- `delay` — per-channel sample-accurate delay (control "Delay (s)"). Use for **speaker time alignment** (FL ahead 1.2 ms vs FR ahead 0.3 ms when driver-seat-focused tuning). Config: `config = { max-delay = 0.05 }`. The control is live-writable; runtime tweak fine.
- `dcblock` — DC-removal high-pass at near-DC; usually unnecessary unless feeding a class-D amp without input cap. Keep in pocket.
- `copy` — when we want one input feeding two parallel chains (e.g. main + sub).
- `mixer` — useful for bass-management: `copy` to a `bq_lowpass` sub-chain + a `bq_highpass` main-chain, then `mixer` to combine. Native 2-way crossover doable today.
- `sofa` spatializer — HRTF placement of mono sources. Not interesting for a car, but it's there.
- `ebur128` + `lufs2gain` — loudness measurement and gain compensation. Worth wiring in front of the playback chain for replay-gain-like auto-leveling between Bluetooth, Android Auto, system sounds.

---

## 10. Gotchas / known issues

- **No graph topology hot-reload.** `module-filter-chain.c` only re-instantiates on rate change (~L1481–L1494) [3]. Adding/removing bands at runtime requires module unload+load. Plan the band count up-front.
- **Biquad history reset on every coefficient change** (`biquad.c` ~L190–L195) [7]. See §4. Mitigation: software-ramp Gain in small steps, don't sweep Freq during playback.
- **Sample-rate mismatch** with the real sink causes the graph to be re-instantiated on every connect — picks the rate of the *first* stream that pins it. If the real sink (e.g. BT A2DP at 44.1k) differs from the typical 48k expectation, force the filter-chain rate via `audio.rate = 48000` and let PW resample at the egress. Or set both equal.
- **`audio.position` mismatch** between `capture.props` and `playback.props` is silently allowed but the channel mapping is then dictated by `inputs`/`outputs` link list — easy to wire L→R by accident. Always set both explicitly.
- **System-instance state**: `pw-cli` writes do NOT persist across pipewire restart. We need a startup script that re-applies the last preset after `pipewire.service` (re)start. Plumb via a oneshot unit reading our JSON and calling `pw-cli`.
- **Filter-chain config search path**: `/usr/share/pipewire/filter-chain.conf.d/*.conf` and `/etc/pipewire/filter-chain.conf.d/*.conf`. Put ours in `/etc/pipewire/filter-chain.conf.d/banks-eq.conf` so they survive image redeploys but are still per-device.
- **Naming the node**: set `playback.props.node.name = "banks_eq"` and `node.description = "Banks Cabin EQ"`. Without explicit `node.name` the IDs shuffle between boots which makes the `pw-cli s <id>` lookup fragile.
- **`pactl list short sinks` won't find it by node.name**; use `pw-dump | jq` or `pw-cli ls Node | grep banks_eq`.
- **`stream.dont-remix` and `audio.channels`** — if a client opens mono, PW auto-upmixes. Force `playback.props.audio.channels = 2` and `stream.dont-remix = false`.
- **`media.class = "Audio/Sink"` vs `"Audio/Duplex"`** — for an output EQ we want Sink. The man page's "Noise Canceling Source" example uses `Audio/Source` with `node.passive = true` which is the *opposite* direction; do not copy-paste that block.

---

## Recommendation

Land **builtin-only `param_eq`, 8 bands stereo, single node, JSON preset, software-ramped live updates** for v1.

```
# /etc/pipewire/filter-chain.conf.d/banks-eq.conf
context.modules = [
  { name = libpipewire-module-filter-chain
    args = {
      node.description = "Banks Cabin EQ"
      media.name       = "Banks Cabin EQ"

      filter.graph = {
        nodes = [
          { type   = builtin
            name   = eq
            label  = param_eq
            config = {
              filters = [
                { type = bq_lowshelf,  freq =    80, gain = 0.0, q = 0.707 }
                { type = bq_peaking,   freq =   160, gain = 0.0, q = 1.0   }
                { type = bq_peaking,   freq =   400, gain = 0.0, q = 1.0   }
                { type = bq_peaking,   freq =  1000, gain = 0.0, q = 1.0   }
                { type = bq_peaking,   freq =  2500, gain = 0.0, q = 1.0   }
                { type = bq_peaking,   freq =  5000, gain = 0.0, q = 1.0   }
                { type = bq_peaking,   freq = 10000, gain = 0.0, q = 1.0   }
                { type = bq_highshelf, freq = 14000, gain = 0.0, q = 0.707 }
              ]
            }
          }
        ]
      }

      capture.props = {
        node.name     = "banks_eq_in"
        node.passive  = false
        media.class   = "Audio/Sink"
        audio.rate    = 48000
        audio.channels = 2
        audio.position = [ FL, FR ]
        node.latency  = "512/48000"
      }
      playback.props = {
        node.name     = "banks_eq_out"
        node.passive  = true
        audio.channels = 2
        audio.position = [ FL, FR ]
        node.latency  = "512/48000"
      }
    }
  }
]
```

- **8 bands** (2 shelves + 6 peaks) covers cabin tuning needs. Bumping to 10 if needed costs nothing.
- **Single `param_eq` node** (not 8 individual biquad nodes) → minimum graph overhead, controls are `Freq N` / `Q N` / `Gain N`.
- **48 kHz fixed**, quantum 512 → ~11 ms each side, ~22 ms round-trip — fine for music, OK for AA voice. Drop to 256 if voice prompts feel laggy.
- **Live updates via `pw-cli s <id> Props { params = [ ... ] }`** — our existing pattern works directly.
- **Preset JSON** (see §7) — file lives at `/etc/banks/eq/presets/<name>.json`. Active preset symlinked at `/var/lib/banks/eq/current.json`.
- **Startup re-apply**: `banks-eq-apply.service` (oneshot, `After=pipewire.service`) reads `current.json` and calls `pw-cli`.
- **Frontend Qt `AudioEQ` page** drives sliders → ramp manager → `pw-cli` (or libpipewire direct via `pw_stream` Props if we ever go in-process). Ramp Gain in 0.25 dB / 30 ms steps to dodge the history-reset click (§4).

### Trade-offs accepted

- No live add/remove bands (config-locked count). Tolerable — 8 is enough.
- Biquad clicks on big steps. Mitigated by ramping in software.
- No oversampling / no analyser FFT vs LSP x16. Add LSP as opt-in v2 only if a user asks.
- No headphone-targeted AutoEQ preset bundling yet — leave the door open via `param_eq` `filename` support.

### Future v2 options (not now)

- Swap `param_eq` for `lsp_para_equalizer_x16_stereo` LV2 for 16 bands + slope control. ~20 KB recipe addition (`lsp-plugins-lv2` with `PACKAGECONFIG = "lv2"`).
- Add a `convolver` node after the EQ for cabin IR correction once we have a measured IR.
- Add per-speaker `delay` nodes for time alignment once we move beyond stereo.
- Replace `pw-cli` shell-out with `libpipewire` direct in-process for sub-millisecond write latency (current is fine but a Qt-native binding is cleaner).

---

## Sources

[1] PipeWire builtin filter descriptors — `src/modules/module-filter-chain/builtin_plugin.c` @ tag 1.0.9
    `https://github.com/PipeWire/pipewire/blob/1.0.9/src/modules/module-filter-chain/builtin_plugin.c`
[2] PipeWire filter-chain module docs (man page) — `docs.pipewire.org/page_module_filter_chain.html`
    `https://docs.pipewire.org/page_module_filter_chain.html`
[3] PipeWire `module-filter-chain.c` Props handling @ tag 1.0.9
    `https://github.com/PipeWire/pipewire/blob/1.0.9/src/modules/module-filter-chain.c`
[4] Arch manpage: `libpipewire-module-filter-chain(7)`
    `https://man.archlinux.org/man/libpipewire-module-filter-chain.7.en`
[5] Debian manpage: filter-chain module
    `https://manpages.debian.org/testing/libpipewire-0.3-modules/libpipewire-module-filter-chain.7.en.html`
[6] openSUSE manpage (Leap 16.0) — filter-chain
    `https://manpages.opensuse.org/Leap-16.0/pipewire-modules-0_3/libpipewire-module-filter-chain.7.en.html`
[7] PipeWire `biquad.c` (`biquad_set()`) @ tag 1.0.9
    `https://github.com/PipeWire/pipewire/blob/1.0.9/src/modules/module-filter-chain/biquad.c`
[8] LSP Plugins — Parametric Equalizer x16 Stereo (LV2)
    `https://lsp-plug.in/plugins/lv2/para_equalizer_x16_stereo`
[9] PipeWire Parametric-Equalizer module docs
    `https://docs.pipewire.org/page_module_parametric_equalizer.html`
[10] PipeWire latency / quantum docs
    `https://docs.pipewire.org/devel/page_latency.html`
[11] asymptotic.io — "Writing a simple PipeWire parametric equalizer module"
    `https://asymptotic.io/blog/pipewire-parametric-autoeq/`
[12] EasyEffects (GitHub) — preset format reference (informational only; not used here)
    `https://github.com/wwmm/easyeffects`
