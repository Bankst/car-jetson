# banks-frontend

Qt6/QML infotainment frontend for the Banks Jetson stack. Hosts projectM as a
`QQuickRhiItem` (`Visualizer`) and an opt-in ImGui debug overlay.

## Architecture

- **Main UI**: Qt6 Quick (QML), Basic style. `StackView` swaps between Home /
  Visualizer / Media / etc. pages.
- **projectM lifecycle**: `Visualizer` QML item — projectM only spins up while
  the item is in the scene graph. `StackView.pop()` destroys it, releasing
  GPU + PipeWire capture. No render = no draw calls (Qt skips dirty pass).
- **Audio**: `AudioCapture` wraps PipeWire capture stream, lock-free SPSC ring,
  fed to projectM each render frame on the render thread. Started/stopped
  via `QQuickItem::itemChange(ItemSceneChange)`.
- **Debug overlay**: ImGui installed at `QQuickWindow` level, on top of all
  QML. Default off. Toggle: `F12`, or env `BANKS_DEBUG_OVERLAY=1`.
- **RHI**: pinned to OpenGL (`QSGRendererInterface::OpenGL`) so projectM +
  ImGui (`imgui_impl_opengl3`, GLES3 path) share Qt's GL context. Required on
  Tegra anyway — only display path is EGL/GLES.

## Threading

- **GUI thread**: QML, properties, user input, command-queue producer.
- **Render thread**: projectM, audio→PCM feed, ImGui draw. Sync point is
  `QQuickRhiItem::synchronize()` (GUI blocked) — only safe time to mutate
  shared state.

## Build

Local dev (Fedora 43):

```sh
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
                 pipewire-devel systemd-devel
cmake --preset default
cmake --build --preset default
./build/banks-frontend
```

Yocto target: add a recipe under
`meta-seeed-jetson/recipes-graphics/banks-frontend/` (TODO — mirror
`banks-viz` layout once that recipe lands).

## Toggles

| CMake option                       | Default | Effect                                  |
|------------------------------------|---------|-----------------------------------------|
| `BANKS_FRONTEND_FETCH_PRESETS`     | ON      | Fetch Cream of the Crop preset pack     |
| `BANKS_FRONTEND_PRESET_INSTALL_DIR`| share/banks-frontend/presets | Install dir, relative to prefix |

## Env

| Var                       | Effect                                      |
|---------------------------|---------------------------------------------|
| `BANKS_DEBUG_OVERLAY=1`   | ImGui overlay visible at startup            |
| `QSG_RHI_BACKEND=opengl`  | Forced by main.cpp regardless               |
| `QT_QPA_PLATFORM=wayland` | Required on Tegra (no X11 DDX)              |
