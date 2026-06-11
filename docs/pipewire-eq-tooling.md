# PipeWire EQ / volume tooling (CLI prototype)

Standalone bash scripts under `scripts/audio/`. Validate the userspace volume + EQ path **before** wiring `AudioMixer` / `AudioEQ` QObjects into banks-frontend. (Research doc: `docs/pipewire-eq-research.md`.)

## Scripts

| File | Purpose |
|---|---|
| `scripts/audio/banks-audio-test.sh` | Non-interactive validation. Discovery → volume ramp/mute → filter-chain install → live param sweep → teardown. |
| `scripts/audio/banks-eq-live.sh` | Interactive live tester. Loops pink noise through filter-chain, keys (1..8 to pick band, -/+ to bump gain) adjust 8-band stereo EQ live with bypass toggle + presets (flat/bass+/voice/treble+/V-shape). |

Runs as root on devkit (system-instance PipeWire). Push via `scp -O <script> root@192.168.55.1:/root/`.

## Architecture proven by tests

- **Filter-chain config** lives in `/etc/pipewire/pipewire.conf.d/99-banks-eq.conf` (drop-in, merges into base `context.modules`). **Single `param_eq` node, stereo via `In 1`/`In 2` + `Out 1`/`Out 2`**, 8 bands chained internally (2 shelves + 6 peaks at 60/150/400/1k/2.5k/6k/10k/15k Hz). Inserts a virtual sink `banks_eq` (`media.class = Audio/Sink`) feeding real hardware sink via `node.passive = true`. One node = one graph entry = minimum PipeWire overhead vs the previous 6-node parallel-chain prototype.
- **Live control** via `pw-cli s <node_id> Props '{ params = [ "eq:Gain 1" <dB> "eq:Gain 2" <dB> ... "eq:Gain 8" <dB> ] }'`. `param_eq` auto-names each band's controls as `Freq N` / `Q N` / `Gain N` (1-indexed). Confirmed by PW 1.0.9 source (`builtin_plugin.c` `param_eq_make_chain()`).
- **Software-ramped Gain writes** (0.25 dB / 30 ms steps) mitigate the `biquad_set()` history-reset click documented in the research doc §4. Freq and Q are config-time only — research warns against live sweeps.
- **Volume + mute** via `wpctl set-volume @DEFAULT_AUDIO_SINK@ <0..1>` / `wpctl set-mute @DEFAULT_AUDIO_SINK@ 0|1`. Standard userspace; no D-Bus bind required from app side.

## Non-obvious deps + gotchas (carry into Yocto recipe + app integration)

1. **Builtin filter labels lack underscores** in section names: `bq_highshelf` / `bq_lowshelf` / `bq_lowpass` / `bq_highpass` / `bq_peaking` / `bq_notch` / `bq_bandpass` / `bq_allpass`. `bq_high_shelf` (with underscore) fails as "cannot find label" and pipewire.service refuses to start — bricks audio until config removed.
2. **`param_eq` is multi-channel native** — supports `In 1..8` / `Out 1..8` ports in a single node. For our stereo case the graph uses `inputs = [ "eq:In 1" "eq:In 2" ]` / `outputs = [ "eq:Out 1" "eq:Out 2" ]`. The per-band `Freq N` / `Q N` / `Gain N` controls apply across all channels of that node, which is exactly what we want for cabin EQ (left/right curves identical). If we ever need independent per-channel curves, switch to two `param_eq` nodes (`eq_l`, `eq_r`) per research doc §5 Pattern A. The old prototype's per-channel biquad nodes (`low_l`/`low_r`/...) are no longer needed.
3. **`pw-cat` has no `--raw` flag.** Stdin reads expect WAV. For streaming gapless audio: write WAV header with max-int `data` size to stdout, then keep streaming raw PCM bytes — `pw-cat -p -` plays continuously until pipe closes. See `banks-eq-live.sh` (`render_pink_gen` + `start_noise` setsid pattern).
4. **Yocto python3 stdlib subset.** `import wave` fails — `python3-audio` not in our image. Workaround: inline RIFF header via `struct.pack`. Same applies to any other rare stdlib module — assume nothing beyond `os`/`sys`/`struct`/`math`/`random` is present.
5. **Image lacks `pactl`** (despite `pipewire-pulse` installed) and **lacks `pkill`** (busybox `ps`/`kill` only). Use `wpctl` for volume, manage child processes via `setsid` + process group kill (`kill -- -$PGID`), avoid `pkill -f` patterns.
6. **Filter-chain config restart sequence**: drop config → `systemctl restart pipewire wireplumber` (NO `pipewire-pulse.service` — unit doesn't exist on our image despite the package being installed). Health-loop poll `systemctl is-active --quiet pipewire && wpctl status` before declaring success. If config rejected, pipewire enters restart loop and audio is fully down until config removed.
7. **`pipewire-tools` + `pipewire-pulse` debs needed** for `pw-cli` / `pw-cat`. Not in base image yet. Push: `scp -O build/tmp/deploy/deb/armv8a_tegra/pipewire-{tools,pulse}_1.0.9*.deb root@target:/tmp/ && jtx ssh 'dpkg -i /tmp/pipewire-tools_*.deb /tmp/pipewire-pulse_*.deb'`. Bake into image before any in-app EQ work.

## Known issues (not yet fixed)

- **Teardown can't always relocate prior default sink** after PW restart — string match between `node.name` and `wpctl status` description column is brittle. WirePlumber's default-routes-policy normally re-elects sensibly; manual `wpctl set-default <id>` is one command. Won't matter once we ship `99-banks-eq.conf` as a permanent drop-in.
- **Stale `Audio/Sink banks_eq` ghost entry** under `wpctl status` Video section after teardown — PW state-store residue. Clears on next full PW restart cycle. Cosmetic.

## Next steps (when resuming)

1. Add presets-from-JSON loader to `banks-eq-live.sh`. Stash presets at `/etc/banks-audio/eq-presets/*.json`.
2. Yocto recipe `meta-seeed-jetson/recipes-multimedia/banks-audio/` shipping `99-banks-eq.conf` + presets dir. Add `pipewire-tools` + `pipewire-pulse` to image packagegroup.
3. Wire `AudioMixer` + `AudioEQ` QObjects into banks-frontend. Mixer talks to `wpctl` via QProcess (or libpipewire). EQ writes `pw-cli s <id> Props '{ params = [ "eq:Gain N" <dB> ... ] }'` for per-band Gain, with the same 0.25 dB / 30 ms software ramp pattern.
4. Settings → Audio QML page with sink picker + master volume + 8-band EQ sliders + preset selector.
