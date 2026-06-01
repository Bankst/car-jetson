# PipeWire upgrade feasibility — 1.0.9 → newer

**Date:** 2026-06-01
**Author:** investigation only — no recipes modified, no commits
**Scope:** Banks Jetson Linux image (scarthgap / meta-tegra `scarthgap-l4t-r35.x`),
devkit `jetson-xavier-nx-banks-devkit` and production `jetson-xavier-nx-a203`.

---

## Executive recommendation

**Target PipeWire 1.4.10 + WirePlumber 0.5.11 (the `whinlatter` meta-openembedded set).
Go.**

Rationale:

- The original premise — "PW 1.2/1.4 added `compressor` and `limiter` builtins, avoiding
  LADSPA/CAPS" — is **wrong**. Only `dcblock` is a builtin (added 2025-01-23 in 1.3.81,
  released stably in 1.4.0, 2025-03-06). `compressor` and `limiter` are still
  **LV2/LADSPA-only** in upstream master as of PW 1.6.0 (2026-02-19). See "Feature gain"
  below.
- That said, 1.4.x is still worth bumping to: it ships `dcblock`, `param_eq` (we already
  use it via filter-chain), `ebur128` (loudness metering — useful as a limiter input),
  `convolver` improvements + fftw-backed convolver (room correction is exactly this), and
  the filter-graph SPA plugin refactor.
- The Yocto recipe upgrade is a near-drop-in. The diff between meta-oe's `scarthgap`
  `pipewire_1.0.9.bb` and `whinlatter` `pipewire_1.4.10.bb` is ~15 lines (mostly SRCREV,
  branch param, one packaging tweak). No PACKAGECONFIG names change for any flag we use
  (`alsa bluez bluez-aac bluez-aptx bluez-ldac bluez-opus systemd pulseaudio pw-cat ...`
  all retained byte-for-byte).
- Our painfully-debugged WP drop-ins (`monitor.bluez.seat-monitoring = disabled`,
  `monitor.alsa.reserve-device = disabled`, `bluez5.profiles = [a2dp-sink]`) survive
  intact: the WP 0.5.x series **does not break any of these config keys** through 0.5.14.
- libfreeaptx + libldac are standalone codec libraries with no PW version coupling. Their
  recipes don't need touching.

**Effort:** small — half a day to write the recipe, one full image rebuild (~1h cold with
icecc), one careful flash + bring-up test pass against the BT/USB-audio matrix.

**Don't jump to 1.6.x / WP 0.5.14** yet — that's `master`, not on any released Yocto
branch, and would require us to maintain a custom recipe long-term.

---

## Version landscape

### Upstream PipeWire

| Version | Date | Notable |
|---|---|---|
| 1.0.9 | 2024-10-22 | **Current shipped** |
| 1.1.x | 2024-05/06 | dev cycle — debug, pipe, zeroramp, noisegate filter builtins |
| 1.2.0 | 2024-06-27 | filter-chain graph code moved to filter-graph SPA plugin; faster convolver |
| 1.2.7 | 2024-11-26 | last 1.2 patch |
| 1.3.81 | 2025-01-23 | dev cycle — **param_eq, ebur128, dcblock** added; fftw convolver |
| 1.4.0 | 2025-03-06 | **stable release of dcblock/param_eq/ebur128/fftw convolver** |
| 1.4.10 | (whinlatter) | latest 1.4.x in any meta-oe release branch |
| 1.5.81 | 2025-01-23 | dev cycle — LV2 plugin gains options+state; ffmpeg avfilter; ONNX |
| 1.5.85 | 2026-01-19 | last 1.5 dev |
| 1.6.0 | 2026-02-19 | **current upstream stable**; max channels compile-time; filter-chain default-channel improvements |

### meta-openembedded recipe map (verified by `git ls-tree`)

| Branch | pipewire | wireplumber | Status |
|---|---|---|---|
| `scarthgap` / `scarthgap-next` | 1.0.9 | 0.5.1 | **What we ship now** |
| `styhead` / `styhead-next` | 1.2.3 | 0.5.6 | Yocto 5.1 (2024-10) |
| `walnascar` / `walnascar-next` | 1.4.1 | 0.5.8 | Yocto 5.2 (2025-04) |
| `whinlatter` | 1.4.10 | 0.5.11 | **Yocto 5.3 (2025-10) — recommended backport target** |
| `master` | 1.6.5 | 0.5.14 | unstable |

### Upstream WirePlumber

| Version | Headline |
|---|---|
| 0.5.1 | **Current shipped** |
| 0.5.2 | external config loading for scripts |
| 0.5.3 | naming consistency, unavailable-profile prevention |
| 0.5.4 | role-based linking policy refactor |
| 0.5.5 | HSP/HFP autoswitch crash hotfix |
| 0.5.6 | component load ordering, multi-instance config |
| 0.5.7 | profile switching fixes, BT headset switching |
| 0.5.8 | UCM SplitPCM, async loopback, node deduplication |
| 0.5.9 | audio node grouping, libcamera, journal logging |
| 0.5.10 | **critical crash fix in linking** (worth having) |
| 0.5.11 | modem manager module, MPRIS pause, localized settings |
| 0.5.12 | mono audio cfg, auto-mute on removal, notifications API |
| 0.5.13 | internal filter graph support, new Lua Properties API, **40% faster linking** |
| 0.5.14 | per-device volume defaults, Lua 5.5 |

No WirePlumber 0.5.x release contains a breaking change to: `monitor.bluez.seat-monitoring`,
`monitor.alsa.reserve-device`, `bluez5.profiles`, `bluez5.autoswitch-to-headset-profile`,
or any of the JSON config syntax we rely on (verified by grep against the official
releases page).

---

## Feature gain — what actually lands by bumping to 1.4.10

| Feature | Released in | Useful for |
|---|---|---|
| `dcblock` filter (builtin) | 1.4.0 | DC offset removal pre-EQ / pre-speaker-protection |
| `param_eq` filter (builtin) | 1.4.0 | We already use it (works on 1.0.9 too — was there pre-stabilization) |
| `ebur128` filter (builtin) | 1.4.0 | Loudness measurement — input to a custom limiter, or for logging |
| `convolver` with FFTW backend | 1.4.0 | **Room correction** — biggest win; current convolver is naive |
| Filter-graph as SPA plugin | 1.2.0 | Slightly cleaner config; allows filter-graph from non-filter-chain contexts |
| Max channels compile-time | 1.5.82 | Not needed (we're 2-ch) |

### Things you were hoping for that are NOT here

- **`compressor` builtin: does not exist** in PipeWire 1.0.9, 1.4.x, or 1.6.x.
- **`limiter` builtin: does not exist** in PipeWire 1.0.9, 1.4.x, or 1.6.x.
- For both, the upstream answer is "use the LV2 host with a compressor/limiter LV2 plugin,
  or use LADSPA via the existing `ladspa` plugin in filter-chain." Confirmed by reading
  the current builtin filter list at `docs.pipewire.org/page_module_filter_chain.html` —
  the documented builtins are: mixer, copy, bq_*, param_eq, convolver, delay, invert,
  clamp, linear, recip, abs, sqrt, exp, log, mult, sine, max, **dcblock**, ramp, debug,
  zeroramp, noisegate, busy, null. No compressor, no limiter, no gain (separate from EQ
  gain).

### Implication for speaker protection plan

Speaker protection still needs either:

1. A LADSPA/CAPS plugin (the original "we want to avoid this" path) — works on 1.0.9 too,
   so this isn't an upgrade-driver.
2. An LV2 plugin — `lv2 = disabled` in the recipe right now; switching to enabled pulls
   in lilv + serd + sord + sratom (≈4 small recipes, all in scarthgap meta-oe). Worth it
   if we're going LV2 anyway for compressor/limiter, **and it's the same cost on 1.0.9 or
   1.4.10**.
3. A custom SPA plugin we write (RMS-tracker + soft-knee limiter — ~200 LOC C).

**Conclusion: the upgrade does NOT relieve us of the compressor/limiter question.** It
does, however, give us a much better convolver and a builtin dcblock, which is real value.

---

## Breaking changes that matter for us

Surveyed PipeWire NEWS 1.0 → 1.6.0 and meta-oe recipe diffs. Filtered to what touches our
config / build.

| Change | Version | Impact for us |
|---|---|---|
| v0 protocol support removed | 1.5.x | None — we don't have legacy v0 clients |
| `client-rt.conf` deprecation warning | 1.4.x | Cosmetic warning. We don't override it. |
| Max channels 64→128 compile-time | 1.5.82 | None — 2-channel only |
| `webrtc-audio-processing-1` → `-2` recipe dep | meta-oe 1.4.x | Auto-handled by recipe; PACKAGECONFIG name unchanged for users |
| `sdl2` recipe dep → `virtual/libsdl2` | meta-oe 1.4.x | We don't enable sdl2 |
| `do_install:append:class-target()` split | meta-oe 1.4.x | Internal — no caller impact |
| `BRANCH = trim_version PV 2` SRC_URI pattern | meta-oe 1.4.x | Already correct shape — we follow upstream |
| filter-chain graph code → SPA plugin | 1.2.0 | Existing `99-banks-eq.conf` syntax is identical |
| filter-chain default channel handling | 1.6.0 | N/A — we target 1.4.10 |
| `bluez5-codec-aptx`/`-ldac`/`-aac` PACKAGECONFIG names | unchanged through 1.6.0 | **Our bbappend stays as-is** |
| systemd unit names (`pipewire.service`, `pipewire-pulse.service`, `wireplumber.service`) | unchanged | Our systemd drop-ins keep working |

WirePlumber 0.5.1 → 0.5.11:

- The Lua-based bluetooth and reserve scripts kept the same hook names. Our JSON
  `wireplumber.conf.d/*.conf` overrides go through the SPA-JSON merge layer which is API-
  stable in 0.5.x.
- `0.5.5` fixed an HSP/HFP autoswitch **crash**. We dodge it via
  `bluez5.profiles = [a2dp-sink]` but the fix is still a nice-to-have.
- `0.5.10` ships a **critical link-creation crash fix**. Independently worth the bump.
- `0.5.13` is `master` (not in `whinlatter`). Skip.

---

## BT codec coupling

- `libfreeaptx_0.2.2` and `libldac_git` are codec libraries linked at PW build time via
  the `bluez-aptx` and `bluez-ldac` PACKAGECONFIG entries. Their ABI is consumed through
  PW's `spa/bluez5/` codec dispatch table, which has been API-stable since 1.0.0.
- Our `pipewire_%.bbappend`:
  ```
  PACKAGECONFIG:append = " bluez-aac bluez-aptx bluez-ldac"
  DEPENDS:append = " libfreeaptx libldac"
  ```
  works **byte-identically** on 1.4.10. Confirmed by examining the meson option names
  (`-Dbluez5-codec-aac/aptx/ldac=enabled`) in both 1.0.9 and 1.4.10 recipes.
- AAC stays gated on `LICENSE_FLAGS_ACCEPTED` containing `commercial` (recipe-driven).

---

## System-instance / startup

- PipeWire 1.0.9 ships `pipewire.service` (system) and `pipewire.service` user unit. Same
  in 1.4.x and 1.6.x. Confirmed by checking `data/systemd/` paths haven't moved.
- WirePlumber's `wireplumber.service` system unit is present in 0.5.x throughout.
- Our `/etc/systemd/system/wireplumber.service.d/headless.conf` (sets
  `DBUS_SESSION_BUS_ADDRESS`) drops in unchanged.
- The pipewire user is created with `--home /` and we have a tmpfiles.d entry creating
  `/.local/state/wireplumber`. None of this changes.

---

## LV2 enable cost

If we decide to go LV2 for compressor/limiter, enabling it adds these recipes (all already
in scarthgap meta-oe `meta-multimedia` / `meta-oe`):

- `lilv` (the LV2 host runtime) — small
- `serd` (RDF lib) — small
- `sord` (RDF graph) — small
- `sratom` (Atom/RDF) — small
- `lv2` (headers + bundles) — small

Decision is independent of the PipeWire bump. Enabling on 1.0.9 vs 1.4.10 is identical in
effort. Recommend deciding LV2 after compressor/limiter plugin candidates are picked.

---

## Recipe approach

**Backport, not bbappend bump.** Reasons:

- The recipe filename embeds the version (`pipewire_1.0.9.bb` vs `pipewire_1.4.10.bb`).
  You can't bump version via bbappend cleanly without `PREFERRED_VERSION` chicanery and
  source URL hacks.
- The cleanest path is to copy meta-oe's `whinlatter` `pipewire_1.4.10.bb` and
  `wireplumber_0.5.11.bb` (plus their `files/` directories) into
  `meta-seeed-jetson/recipes-multimedia/pipewire/` and
  `meta-seeed-jetson/recipes-multimedia/wireplumber/`. Pin via
  `PREFERRED_VERSION_pipewire = "1.4.10"` and `PREFERRED_VERSION_wireplumber = "0.5.11"`
  in `meta-seeed-jetson/conf/distro/banks-jetson.conf` (or layer.conf).
- Our existing `pipewire_%.bbappend` and `wireplumber_%.bbappend` are version-agnostic
  (`%` wildcard) — they match either. **No bbappend changes needed** for the codec/headless
  setup.
- Track upstream meta-oe `walnascar`/`whinlatter` branches in git for any patch
  back-ports. When meta-oe `scarthgap-next` eventually bumps (if ever — unlikely on an
  LTS), we delete our copies.

### Sanity check before copy

Before copying, also bring across any companion `pipewire-media-session_0.4.2.bb` /
`pipewire-0.2_git.bb` if they're depended on. They aren't in our image, so don't.

---

## Risk register

| Risk | Severity | Probability | Mitigation |
|---|---|---|---|
| BT pairing regression (A2DP source → sink) | **High** | Low | WP 0.5.x BT stack is API-stable; smoke test phone-pair-stream-AVRCP on devkit first |
| BT codec negotiation regression (aptX/LDAC fallback to SBC) | Medium | Low | Verify `pw-cli list-objects` shows codec node post-pair; capture `wpctl status` |
| HFP cycling reappears | Medium | Low | Our `bluez5.profiles = [a2dp-sink]` still applies; 0.5.5 fixed the underlying crash anyway |
| ALSA monitor regression on USB headset | Medium | Low | Our `monitor.alsa.reserve-device = disabled` still applies; bullet-proof with `wpctl status` |
| WirePlumber headless DBUS_SESSION_BUS_ADDRESS workaround stops being needed | Low | Low | Even if it becomes a no-op, the env var is harmless |
| `dcblock` config in `99-banks-eq.conf` rejected by 1.0.9 (testing locally before deploy) | Low | High | Use 1.4.10 only on devkit; keep 1.0.9 on A203 for the test window |
| Filter-chain `param_eq` semantics drift (Gain N control names) | Low | Low | Already stable in 1.0.9; upstream commits don't touch the auto-naming |
| `EXTRA_OEMESON` flag rename | Low | Low | Diff shows none in our enabled set |
| webrtc-audio-processing version bump pulled in transitively | Low | Medium | meta-oe whinlatter already has the matching `-2` package |
| **First flash + boot fails — image-level breakage** | **High** | Low | Boot-recovery via existing `initrd-flash --erase-nvme` rolls forward to the known-good rootfs in <15min |
| Reduced FFTW perf-vs-power on Xavier NX (room correction CPU spike) | Low | Medium | Profile with `pw-top`; convolver block size is tunable in config |

---

## Effort estimate

| Task | Time |
|---|---|
| Copy meta-oe `whinlatter` recipes (`pipewire_1.4.10.bb`, `wireplumber_0.5.11.bb`, `files/`) into `meta-seeed-jetson/recipes-multimedia/` | 30 min |
| Add `PREFERRED_VERSION_*` pins to `banks-jetson.conf` | 5 min |
| Verify our `pipewire_%.bbappend` and `wireplumber_%.bbappend` apply (sstate inspection + `bitbake -e | grep ^PV`) | 15 min |
| First clean build of `banks-jetson-image-base` for devkit | 60 min (icecc warm); 4h cold |
| Flash devkit, smoke-test BT A2DP + USB headset + filter-chain + `wpctl status` | 60 min |
| Validate dcblock + convolver-fftw in a sample filter-chain config | 60 min |
| Backport to A203 production machine, regression test | 60 min |
| Doc update (CLAUDE.md "Stack snapshot" + this file moves to "done") | 15 min |
| **Total** | **~5–6 hours wall clock, ~2 hours active** |

---

## Stepwise validation plan

1. **Read this doc end-to-end first.** Catch errors before they cost a rebuild.
2. **Snapshot known-good build artifacts.** Tar `build/tmp/deploy/deb/armv8a_tegra/{pipewire,wireplumber}_*.deb` and `build/tmp/deploy/images/jetson-xavier-nx-banks-devkit/` so we can roll back debs onto a running board if needed.
3. **Branch the work.** New branch `feat/pipewire-1.4.10`. Do NOT do this on main or the current `feat/banks-frontend-qt6` branch.
4. **Copy recipes.** From `meta-openembedded/` worktree on `origin/whinlatter`: `pipewire_1.4.10.bb`, `wireplumber_0.5.11.bb`, and their `files/` directories. Drop into `meta-seeed-jetson/recipes-multimedia/pipewire/` and `.../wireplumber/`. Keep original meta-oe copies intact (in their upstream layer).
5. **Pin versions.** Add to `meta-seeed-jetson/conf/distro/banks-jetson.conf`:
   ```
   PREFERRED_VERSION_pipewire = "1.4.10"
   PREFERRED_VERSION_wireplumber = "0.5.11"
   ```
6. **Verify bbappend still matches.** `bitbake -e pipewire | grep -E '^(PV|PACKAGECONFIG)='` — confirm 1.4.10 + bluez-aac/aptx/ldac present. Same for wireplumber, confirm 0.5.11 + our config drop-ins listed in SRC_URI.
7. **Build for devkit only first.** `kas-icecc build kas/base.yml` (with `MACHINE=jetson-xavier-nx-banks-devkit` override in kas file or env). Expect a wide sstate miss — the SRCREV/version change cascades through every recipe that depends on PW (gstreamer1.0-pipewire, etc.).
8. **Flash devkit.** Standard `initrd-flash` flow. Confirm boot, journal shows `pipewire.service` + `wireplumber.service` active.
9. **Audio smoke matrix** — run on devkit:
   - USB headset: `wpctl status` shows it as a sink; `pw-cat -p test.wav` plays
   - BT A2DP from phone: pair (Numeric Comparison via bt-audio-agent), `wpctl status` shows codec, audio plays via `banks_eq` filter-chain
   - aptX/LDAC negotiation: phone-side codec selector, then `pw-cli ls Node | grep codec`
   - HFP not cycling: `journalctl -fu wireplumber` quiet for 60s after pair
   - I2S5 + PCM5102A: `aplay -D plughw:APE,0 test.wav` after XBAR routing
10. **Filter-chain smoke** — push new `99-banks-eq.conf` with `dcblock` + fftw convolver enabled, restart pipewire, verify load via `pw-dump | jq '.[] | select(.info.props."node.name" == "banks_eq")'`.
11. **A203 regression.** Repeat steps 7–9 with A203 MACHINE. (No I2S — only USB + BT to exercise.)
12. **Doc + commit.** Update CLAUDE.md "Stack snapshot" table. Squash into a single
    "pipewire: bump to 1.4.10 / wireplumber 0.5.11 (backport from whinlatter)" commit.
    Update this file's executive summary to "done" with the as-built PV.

---

## Notes for future-us

- If meta-oe ever publishes `scarthgap-next` updates that include the PW bump, our local
  copy becomes stale and `bitbake-layers` will flag a duplicate. Delete the local copy
  when that happens.
- 1.6.x bump (and WP 0.5.13+) is **not recommended** until `walnascar`-or-later becomes
  our Yocto base. The 1.6 channel-handling changes are unlikely to bite us in stereo, but
  the "internal filter graph" WP refactor in 0.5.13 might require revisiting our
  drop-ins.
- The "speaker protection / limiter / compressor" work still needs a separate decision:
  LV2 plugin host, LADSPA plugin host (already supported by 1.0.9), or custom SPA plugin.
  The upgrade does **not** answer this question.

## Sources

- meta-openembedded recipe tree (verified locally via `git ls-tree`)
- PipeWire NEWS: https://github.com/PipeWire/pipewire/blob/master/NEWS
- PipeWire filter-chain doc: https://docs.pipewire.org/page_module_filter_chain.html
- WirePlumber releases page: https://pipewire.pages.freedesktop.org/wireplumber/resources/releases.html
- Local recipe diff: `meta-openembedded/meta-multimedia/recipes-multimedia/pipewire/pipewire_1.0.9.bb` vs `origin/whinlatter:.../pipewire_1.4.10.bb`
