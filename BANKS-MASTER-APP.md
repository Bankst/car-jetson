# Banks Master App — Handoff Document

Native development reference for a new Wayland/GLES3 app that combines projectM visualization, BT AVRCP media controls, and PipeWire-native audio capture into a single ImGui-based interface.

**Verified 2026-05-21**: libprojectm 4.2.0+git built, flashed, and running on Xavier NX. FBO API (`projectm_opengl_render_frame_fbo`) confirmed in .so. frontend-sdl2 renders presets on Tegra GLES. Host (Fedora 43, AMD Radeon 880M) has identical subsystems — no stubs needed.

## Target Hardware

| Property | Value |
|---|---|
| SoC | NVIDIA Jetson Xavier NX (Carmel ARMv8.2, 6-core) |
| GPU | Volta (384 CUDA cores, no Turing/Ampere features) |
| Graphics API | **GLES 3.2** only — no desktop GL. No GLX. No X11 DDX driver. |
| Display path | Wayland via EGL/GBM → Weston 10 (DRM backend, `tegra_udrm`) |
| DRI device | `/dev/dri/card0` = `tegra_udrm` (NVIDIA KMS-less DRM, no modesetting) |
| Compositor | Weston 10.0.2 (meta-tegra pins this version), kiosk-shell or desktop-shell |
| Wayland socket | `wayland-kiosk` at `XDG_RUNTIME_DIR=/run/kiosk` |
| Display output | HDMI (single output on A203 V2 carrier) |
| Audio | PipeWire (system service, headless) + WirePlumber |
| BT audio | A2DP sink (SBC/AAC/aptX/LDAC), BlueZ 5, AVRCP for metadata |
| OS | Yocto scarthgap (glibc, systemd, L4T R35.6.4 / JetPack 5.1.6) |

## Host Dev Machine (Fedora 43, Framework 13)

All subsystems match target — same code paths, no conditional compilation needed.

| Subsystem | Host | Target | Notes |
|---|---|---|---|
| GLES 3.2 | Mesa 25.3.6 | NVIDIA Tegra | Same API surface |
| Wayland | KDE/KWin | Weston 10 | Same protocol |
| EGL | Mesa EGL | NVIDIA EGL/GBM | Same API |
| PipeWire | 1.4.11 (user session) | 1.x (system service) | Same API, different socket path |
| WirePlumber | 0.5.14 | ~same | Same |
| BlueZ | 5.86 | 5.x | Same D-Bus API |
| sd-bus | systemd 258 | ~254 | Same API |
| SDL2 | needs `dnf install` | in image | Same |

Only runtime difference: `PIPEWIRE_RUNTIME_DIR` — unset on host (user session default), `/run/pipewire` on target.

## Architecture Overview

```
┌─────────────────────────────────────────────┐
│              SDL2 Window (Wayland)           │
│  ┌────────────────────┐ ┌─────────────────┐ │
│  │                    │ │  ImGui Panels    │ │
│  │   projectM FBO     │ │  - Track info    │ │
│  │   (rendered as     │ │  - Play/pause    │ │
│  │    ImGui::Image)   │ │  - Volume        │ │
│  │                    │ │  - Preset picker  │ │
│  │                    │ │  - BT devices    │ │
│  └────────────────────┘ └─────────────────┘ │
└─────────────────────────────────────────────┘
        │                         │
   libprojectm               D-Bus (BlueZ)
   render to FBO              AVRCP metadata
        │                         │
   PipeWire ──────────────── audio capture
   (pw_stream, native)       (monitor node)
```

## Display / EGL Constraints

### What works on Tegra
- SDL2 with Wayland backend (`SDL_VIDEODRIVER=wayland`)
- EGL context via `eglGetDisplay(EGL_DEFAULT_DISPLAY)` or `SDL_GL_CreateContext`
- GLES 3.0/3.2 — use `SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)`
- ImGui with `imgui_impl_sdl2` + `imgui_impl_opengl3` using `#version 300 es`

### What does NOT work
- Desktop OpenGL (`#version 150`, `OpenGL::GL`, `GL/gl.h`)
- X11 anything (no DDX driver on L4T R35)
- GLX headers
- `xf86-video-modesetting` (finds `tegra_udrm` but no KMS connectors → "no screens found")

### GLES / desktop GL compatibility
Host Mesa supports both GLES 3.2 and desktop GL 4.6. Build with GLES on both platforms — no `#ifdef` needed. The GLES path is the only one that works on target, and it works identically on host via Mesa.

```c
#define IMGUI_IMPL_OPENGL_ES3
const char* glsl_version = "#version 300 es";
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
```

## projectM Integration

### Library version
**libprojectm 4.2.0+git** — pinned to master commit `4d28493` in Yocto recipe `libprojectm_git.bb`. Verified on target: FBO symbol present, frontend-sdl2 renders presets.

Upstream replaced SOIL2 with stb_image and added GLAD as GL loader. No GLES patches needed anymore.

### Available render API
```c
// Render to default framebuffer (existing, since 4.0.0)
projectm_opengl_render_frame(instance);

// Render to user-provided FBO (since 4.2.0) — THE KEY API
projectm_opengl_render_frame_fbo(instance, fbo_id);

// Burn external texture into the active preset (since 4.2.0)
projectm_opengl_burn_texture(instance, texture_id, left, top, width, height);
```

### Render-to-texture pattern
```c
// Setup
GLuint fbo, tex, depth;
glGenFramebuffers(1, &fbo);
glGenTextures(1, &tex);
glGenRenderbuffers(1, &depth);

glBindTexture(GL_TEXTURE_2D, tex);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viz_w, viz_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

glBindRenderbuffer(GL_RENDERBUFFER, depth);
glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viz_w, viz_h);

glBindFramebuffer(GL_FRAMEBUFFER, fbo);
glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

// Per frame
projectm_opengl_render_frame_fbo(pm, fbo);

// In ImGui
ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(viz_w, viz_h));
```

### projectM config (relevant settings)
```c
projectm_set_mesh_size(pm, 48, 32);
projectm_set_fps(pm, 60);
projectm_set_preset_duration(pm, 30.0);
projectm_set_soft_cut_duration(pm, 3.0);
projectm_set_hard_cut_enabled(pm, true);
projectm_set_beat_sensitivity(pm, 1.0f);
projectm_set_window_size(pm, viz_w, viz_h);
```

### Presets
9795 Milkdrop `.milk` presets from [presets-cream-of-the-crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop) verified working on target at `/usr/share/projectMSDL/presets/`.

For native dev, clone same repo into project `presets/` dir.

## PipeWire Native Audio Capture

### Why native instead of SDL audio
SDL audio → PulseAudio compat layer → PipeWire. Extra hop, can't pick specific nodes, higher latency. Confirmed: frontend-sdl2's SDL audio capture fails on target ("Failed to open audio device... No such file or directory") because it tries ALSA directly. Native `pw_stream` taps the graph directly and works.

### Architecture on target
- PipeWire runs as **system service** (not user session — headless Jetson, no logind seat)
- `PIPEWIRE_RUNTIME_DIR=/run/pipewire`
- WirePlumber manages policy
- BT A2DP audio arrives via BlueZ → PipeWire bluez5 module → `bluez_input.<mac>` node
- Null audio sink always exists as fallback

### PipeWire capture code pattern
```c
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

// Capture callback — called per audio buffer
static void on_process(void *userdata) {
    struct pw_buffer *b;
    struct spa_buffer *buf;
    float *samples;
    uint32_t n_samples;

    b = pw_stream_dequeue_buffer(stream);
    if (!b) return;

    buf = b->buffer;
    samples = buf->datas[0].data;
    n_samples = buf->datas[0].chunk->size / sizeof(float);

    // Feed to projectM
    projectm_pcm_add_float(pm, samples, n_samples, PROJECTM_MONO);

    pw_stream_queue_buffer(stream, b);
}

// Setup
pw_init(NULL, NULL);
struct pw_main_loop *loop = pw_main_loop_new(NULL);
struct pw_stream *stream = pw_stream_new_simple(
    pw_main_loop_get_loop(loop),
    "banks-viz-capture",
    pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_STREAM_CAPTURE_SINK, "true",  // capture sink monitor
        PW_KEY_NODE_TARGET, "null-audio-sink", // or specific BT node
        NULL),
    &stream_events,
    NULL);

// Connect as capture
uint8_t params_buffer[1024];
struct spa_pod_builder b = SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));
const struct spa_pod *params[1];
params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
    &SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_F32,
        .channels = 2,
        .rate = 44100));

pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
    params, 1);
```

This code works identically on host (Fedora PipeWire user session) and target (Jetson PipeWire system service).

## BT AVRCP Media Control (D-Bus)

### D-Bus interfaces used
All on the **system bus** (`--system`).

| Interface | Path pattern | What it does |
|---|---|---|
| `org.bluez.MediaPlayer1` | `/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX/playerN` | Track metadata + playback control |
| `org.bluez.MediaTransport1` | `/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX/sepN/fdN` | Transport volume, codec info |
| `org.bluez.Device1` | `/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX` | Device alias (friendly name) |

### Finding the player
```
dbus-send --system --dest=org.bluez --print-reply \
  / org.freedesktop.DBus.ObjectManager.GetManagedObjects
```
Grep for paths matching `/org/bluez/hci\d+/dev_[A-F0-9_]+/player\d+`.

### Getting track metadata
```
dbus-send --system --dest=org.bluez --print-reply \
  /org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX/player0 \
  org.freedesktop.DBus.Properties.GetAll string:org.bluez.MediaPlayer1
```
Returns: `Status` (playing/paused/stopped), `Title`, `Artist`, `Album`, `TrackNumber`, `NumberOfTracks`, `Duration`, `Position` (all in ms).

### Playback commands
```
dbus-send --system --dest=org.bluez --print-reply \
  <player_path> org.bluez.MediaPlayer1.Play
  <player_path> org.bluez.MediaPlayer1.Pause
  <player_path> org.bluez.MediaPlayer1.Next
  <player_path> org.bluez.MediaPlayer1.Previous
```

### Real-time updates via dbus-monitor
```
dbus-monitor --system \
  "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged'"
```
Filter lines containing `MediaPlayer1` (track change) or `MediaTransport1` (BT volume change).

### Volume control (PipeWire/WirePlumber)
```sh
# Get
wpctl get-volume @DEFAULT_AUDIO_SINK@
# Set (relative)
wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 0.05+
wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 0.05-
# Mute toggle
wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle
```
Requires `PIPEWIRE_RUNTIME_DIR=/run/pipewire` in env on target (system service, not user session).

### C library for D-Bus
Use **sd-bus** (libsystemd) — already on both host and target, zero extra deps, clean C API.

```c
#include <systemd/sd-bus.h>
sd_bus *bus;
sd_bus_open_system(&bus);
// property reads via sd_bus_get_property_string() etc.
```

Host has BlueZ 5.86 with paired devices (WH-1000XM4, etc.) — connect headphones, play music, and MediaPlayer1 appears on system bus. Same code path as target.

## Build Dependencies

```sh
# Fedora 43 (one-time)
sudo dnf install -y \
  SDL2-devel \
  pipewire-devel \
  systemd-devel \
  freetype-devel \
  glm-devel \
  mesa-libGLES-devel \
  mesa-libEGL-devel \
  cmake ninja-build gcc-c++

# Build libprojectm from source (4.2.0+ for FBO API)
git clone https://github.com/projectM-visualizer/projectm.git
cd projectm && mkdir build && cd build
cmake .. -DENABLE_GLES=ON -DBUILD_TESTING=OFF -DENABLE_SYSTEM_GLM=ON
make -j$(nproc) && sudo make install

# ImGui — vendored, not a system package
git clone https://github.com/ocornut/imgui.git vendor/imgui
```

## Suggested Project Structure

```
banks-viz/
  CMakeLists.txt
  src/
    main.cpp                 # SDL2 init, main loop, shutdown
    renderer.cpp/.h          # FBO setup, projectM lifecycle, ImGui frame
    audio_capture.cpp/.h     # PipeWire stream → projectM PCM feed
    media_control.cpp/.h     # sd-bus AVRCP: metadata + playback commands
    volume_control.cpp/.h    # wpctl wrapper or direct PipeWire volume
    gui.cpp/.h               # ImGui panels: track info, controls, viz frame, presets
  vendor/
    imgui/                   # vendored
  presets/                   # .milk files for testing
```

## Cross-compile & Deploy

### Option A: Yocto recipe (full integration)
Write a `banks-viz.bb` recipe. Inherit cmake. DEPENDS on libprojectm, libsdl2, pipewire, systemd. Build via kas-container. Deploy via `jtx push banks-viz`.

### Option B: Cross-compile with SDK
```sh
# Extract Yocto SDK (one-time)
kas-container shell kas/base.yml -c "bitbake banks-jetson-image-base -c populate_sdk"
# Source the SDK env
source /opt/poky/.../environment-setup-aarch64-poky-linux
# Build
cmake -B build-tegra -DENABLE_GLES=ON
cmake --build build-tegra
# Deploy
scp -O build-tegra/banks-viz root@192.168.55.1:/usr/local/bin/
```

Note: `scp` requires `-O` flag (legacy protocol) — Jetson's OpenSSH sftp subsystem has a quirk with the new scp protocol.

## Runtime Environment on Target

```sh
# Required env vars (kiosk.service sets these)
export XDG_RUNTIME_DIR=/run/kiosk
export WAYLAND_DISPLAY=wayland-kiosk
export PIPEWIRE_RUNTIME_DIR=/run/pipewire

# Launch
/usr/local/bin/banks-viz
```

The kiosk-launcher starts Weston, then execs whatever `/etc/kiosk/kiosk-app` points to. Swap that symlink to `banks-viz` when ready.

## Known Tegra Gotchas

1. **GLSL `#version 300 es`** — all shaders must use ES profile. ImGui's GL3 backend defaults to `#version 150`. Pass `"#version 300 es"` to `ImGui_ImplOpenGL3_Init()`.

2. **No `GL_RGBA8` in GLES 3.0** as a renderbuffer format — use `GL_RGBA8` only for textures. For renderbuffers use `GL_DEPTH24_STENCIL8` (supported).

3. **`tegra_udrm`** — NVIDIA's custom DRM device. EGL works through it, but libdrm ioctls are NVIDIA-specific (remapped via `libdrm-nvdc` in meta-tegra). Don't use raw libdrm.

4. **Weston 10 only** — meta-tegra pins this. No wlroots, no Sway, no Weston 13+.

5. **System-service PipeWire** — no logind seat, no `XDG_RUNTIME_DIR` from PAM. `PIPEWIRE_RUNTIME_DIR=/run/pipewire` is mandatory.

6. **SDL audio capture fails** — frontend-sdl2 confirmed: "Failed to open audio device... No such file or directory". SDL tries ALSA directly, bypassing PipeWire. Master app must use native `pw_stream`.

7. **scp needs `-O` flag** — Jetson's sshd + host's newer scp default to SFTP protocol which fails. Use `scp -O` for legacy protocol.

8. **First boot after flash** — USB gadget may not come up until second boot. Known `run-postinsts` race with `ldconfig`. Reboot resolves.
