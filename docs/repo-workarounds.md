# Non-obvious workarounds in this repo (so future-you/me doesn't redo them)

1. **`KAS_RUNTIME_ARGS="--security-opt label=disable"`** is mandatory on Fedora enforcing. Without it the kas-container can't read `/repo` due to SELinux MCS labelling on bind mounts. Only mode that works is disabling label confinement for the container; `:Z` mount option fights kas-container's own `-v` lines.

2. **KDE/KF6 master branches drifted** — `yocto-meta-kde` does `inherit kf6-cmake` (hyphenated), `yocto-meta-kf6` ships `kf6_cmake.bbclass` (underscored). Our shims in `meta-seeed-jetson/classes/kf6-*.bbclass` bridge the gap. Remove if upstream syncs.

3. **`LAYERSERIES_COMPAT` override** for `qt6-layer`/`kf6`/`kde` lives in `meta-seeed-jetson/conf/layer.conf`, NOT in `local.conf`. The compat check runs after layer.confs but before local.conf is read. Their layer.confs claim "styhead walnascar" only, but KDE's own `yocto-manifest/scarthgap.xml` confirms master-on-scarthgap is the supported pairing.

4. **Bootloader: prebuilt UEFI** (`tegra-uefi-prebuilt`) instead of source-built EDK2. Source build of `edk2-firmware-tegra` on scarthgap GCC 13 fails with `GenFw: ERROR 3000: DOS header signature was not found in UiApp.dll` — PE-COFF conversion bug, likely `-flto` interaction. Prebuilt is what NVIDIA ships for L4T R35.6.4 and is well-validated.

5. **Don't `PACKAGECONFIG:remove "resolved"` from systemd** — it breaks `nss-resolve` which depends on it. NetworkManager and `systemd-resolved` coexist fine; resolved just provides DNS stub. We only remove `networkd` (and even that's currently *not* removed since we kept it available for the `l4t-usb-device-mode` recipe path, which we don't use anymore — the remove can be re-added if you want a leaner image).

6. **NetworkManager `nmtui` is gated by PACKAGECONFIG** — the `networkmanager-nmtui` package only exists if `nmtui` is in PACKAGECONFIG. We add it via `PACKAGECONFIG:append:pn-networkmanager = " nmtui"` in distro conf.

7. **A203 DTBs are byte-identical drop-ins** — Seeed kept NVIDIA's filenames (`tegra194-p3668-*.dtb`, `tegra19x-mb1-pinmux-p3668-a01.cfg`) so substitution is just "ship our copies; bbappend the kernel `do_deploy` to overwrite the kernel-built ones." Confirmed via md5: deployed DTB matches source 1:1.

8. **Cellular Quectel modem userspace + audio init script from the driver pack — DEFERRED.** Quectel is in `203_jp514.tar.gz` under `rootfs/leetop/quectel/`; A203 audio init is `code_spkmic.sh + startup.service`. Re-package both as recipes only if user actually needs them.

9. **USB gadget approach: NM owns the bridge.** We do *not* install meta-tegra's `l4t-usb-device-mode` recipe — it ships only systemd-networkd `.network`/`.netdev` files (and even then it's incomplete — the actual gadget creation script is missing from meta-tegra; NVIDIA ships it in their `nv-l4t-usb-device-mode` deb which meta-tegra doesn't pull in). Our setup: a small `banks-usb-gadget.sh` configfs script + systemd unit + NM keyfiles for the bridge, all under `recipes-bsp/banks-jetson-iface/` (carrier-agnostic; works on A203 and devkit P3509).

10. **GCC 13 vs L4T 5.10 kernel — three layers of fixes required.** L4T 5.10 was authored for GCC 11; scarthgap GCC 13 promotes four new warning classes to errors. Fixes:
    - **KCFLAGS** in `linux-tegra_%.bbappend`: `KCFLAGS='-Wno-address -Wno-implicit-fallthrough -Wno-int-in-bool-context -Wno-tautological-compare'` covers in-tree warnings from trace macros, ACPI, nvidia display, and the r8168 OOT module.
    - **Patch `0002-nvgpu-drop-gcc13-implicit-fallthrough-override.patch`**: nvgpu's `Makefile` explicitly adds `ccflags-y += $(call cc-option, -Wimplicit-fallthrough=3)` which appends *after* KCFLAGS in the compiler invocation, overriding the suppression. The patch removes that line.
    - **ATF bbappend** (`recipes-bsp/arm-trusted-firmware/arm-trusted-firmware_%.bbappend`): ATF's build system collects `-Werror` in its own `ERRORS` make variable (Makefile line 409), not KCFLAGS. `EXTRA_CFLAGS` has no effect. Fix: `EXTRA_OEMAKE:append = " ERRORS='-Werror -Wno-error=logical-op'"`.
    - These were exposed all at once because `INHERIT += "icecc"` changes all task hashes → full sstate miss → first icecc build rebuilt everything from scratch.

11. **Serial-tegra console patch: exists but NOT wired — causes initrd-flash lockup.** `linux-tegra/0001-serial-tegra-add-console-and-earlycon-support.patch` adds earlycon + late console to the tegra-hsuart driver. The patch is syntactically correct and compiled cleanly, but the initrd kernel (used by `initrd-flash`) locked up on flash. The patch is kept for future reference but is intentionally absent from `SRC_URI`. The `console=ttyTHS0,115200n8` and `earlycon=...` kernel args are also absent from `banks-jetson.conf`. Serial getty on `ttyTHS0` still works via the userspace `serial-getty@ttyTHS0.service` symlink in the image recipe — no kernel patch needed for that.

12. **Stale `bitbake.lock` / `bitbake.sock` block new builds after unclean container exit.** If `kas-container` is Ctrl-C'd or OOM-killed, these files remain in `build/`. The next `kas-container build` will hang waiting for the lock. Fix: `rm -f build/bitbake.lock build/bitbake.sock` before relaunching.

13. **`~/bstat` script for build status without prompting Claude.** Quick tail of the most recent `/tmp/r3-build-*.log` with error count. See the script at `~/bstat`.

14. **SSH login latency — root cause and fix.** Fresh SSH login was ~1.85s. Investigation (via strace bisection of PAM modules) revealed three compounding causes:
    - **pam_unix in PAM auth stack**: OpenSSH with `UsePAM yes` always runs `pam_authenticate()` in a dedicated pthread, communicating via a pipe IPC with the main sshd thread. pam_unix triggers this conversation mechanism even for pubkey/empty-password logins, adding ~0.6s on every connection regardless of auth method. Fix: **`UsePAM no`** — OpenSSH handles passwords natively via `/etc/shadow`; no PAM thread, no IPC.
    - **sntrup761 post-quantum KEX**: OpenSSH 9.x defaults to `sntrup761x25519-sha512` for key exchange. This hybrid post-quantum algorithm is expensive on ARM Carmel, adding ~170ms. Fix: `KexAlgorithms curve25519-sha256,ecdh-sha2-nistp256` in sshd_config.
    - **Socket-activated sshd**: `sshd.socket` + `sshd@.service` spawns a new sshd process per connection. Fix: pre-forked `sshd.service` (`sshd -D`); `sshd.socket` masked in image.
    - Result: **~0.20s** fresh pubkey login. Password auth works natively with `UsePAM no` + `PasswordAuthentication yes`.
    - `UsePrivilegeSeparation no` was also tested (~100ms saving) but the option was removed in OpenSSH 9.x — don't add it to sshd_config, sshd will refuse to start.
    - Logind session tracking lost with `UsePAM no` (no pam_systemd). XDG `/run/user/UID` covered by `loginctl enable-linger <user>`.
    - Baked in: `meta-seeed-jetson/recipes-connectivity/openssh/openssh_%.bbappend` ships custom `sshd_config` + `sshd.service`; image recipe masks `sshd.socket` and enables `sshd.service`.
    - **Strace artifacts**: measuring with `strace -e trace=all` caused `close_range()` to fail under ptrace → sshd fell back to 65535-iteration `close()` loop, making strace look like the bottleneck. Always filter strace traces (`-e trace=close,close_range` etc.) when measuring timing.

15. **`initrd-flash` prompts for password mid-flash.** `initrd-flash` uses `udisksctl mount` to access USB storage during the "create partitions" step. Polkit requires auth for `org.freedesktop.udisks2.filesystem-mount` without an active desktop session. Fix: `scripts/setup-host-flash-permissions.sh` installs a polkit rule allowing `wheel` group to mount without password. Run once on the build host.

16. **weston-terminal 10 has no mouse/touch reporting.** Curses `mousemask()` works but weston-terminal never sends mouse escape sequences to the pty. Fix: use `matchbox-terminal` (GTK3 + VTE, Wayland-native) which has proper mouse reporting. `foot` terminal not in any OE layer.

17. **Persistent data across reflash — UDA partition.** NVMe partition 15 (`PARTLABEL=UDA`, 400MB) is part of NVIDIA's standard flash layout and is unused by default. `banks-persist` recipe formats it as ext4 on first boot, mounts at `/data`, and bind-mounts `/var/lib/bluetooth` + SSH host keys from it. BT pairing keys and SSH host keys survive rootfs reflash (when flashing without `--erase-nvme`). First flash with `--erase-nvme` creates UDA empty; first boot initializes it; subsequent flashes without `--erase-nvme` preserve it. Recipe in `recipes-core/banks-persist/`, added to `packagegroup-seeed-base` so all images get it.
    - **UDA mount**: fstab entry with `nofail,x-systemd.device-timeout=30` — added by `banks-persist-setup` on first boot. Setup service uses `Wants=dev-disk-by\x2dpartlabel-UDA.device` to wait for NVMe enumeration.
    - **SSH persistence**: bind-mount individual `ssh_host_*` key files, NOT the entire `/etc/ssh/`. Binding the whole dir overwrites rootfs `sshd_config` with stale UDA copy, reverting UsePAM/KEX optimizations.
    - **Triggerhappy socket activation**: must be masked in kiosk image. Socket-activated `thd` ignores `--deviceglob` and waits for `th-cmd --passfd` from udev — keyboard hotkeys silently stop working.

18. **Tegra SoC audio is machine-gated.** A203 V2 carrier has no I2S DAC / DMIC / DSPK pins routed, so the Tegra audio crossbar (AHUB / ADMAIF / ADSP / I2S / DMIC / DSPK / AMX / ADX / SFC / MVC / MIXER / AFC / IQC / OPE / ARAD / ASRC) is **disabled** there via `no-audio-soc.cfg`. NVIDIA devkit P3509 (`jetson-xavier-nx-banks-devkit`) exposes I2S5 on the 40-pin header (DAP5: pin 12 BCLK, 35 LRCLK, 40 SDATA), so it gets the full SoC audio stack via `audio-soc.cfg`. Gating lives in `linux-tegra_%.bbappend`:
    ```
    SRC_URI:append:jetson-xavier-nx-a203          = " file://no-audio-soc.cfg"
    SRC_URI:append:jetson-xavier-nx-banks-devkit  = " file://audio-soc.cfg"
    ```
    - **Why off on A203**: ~17 modules loading during udev coldplug, plus `tegra210_adsp` driver iterating FE/BE DAI links from the generic `tegrasndt186ref` machine driver and spamming `Broken Path1 - FE not linked to BE` (~14 messages, ~3s of post-login log churn) because the DT enables ADSP DAI nodes that the machine driver doesn't pair with any BE on a board with no I2S routing. Even on devkit you'll still see this spam at boot — cosmetic only. Silence later via DT overlay (`status = "disabled"` on unused `tegra210-adsp-audio` children) or by demoting `dev_err → dev_dbg` in `tegra-alt/tegra210_adsp_alt.c:1409,1734`.
    - **A203 keepers**: `SND_HDA_TEGRA` (HDMI audio out), `SND_USB_AUDIO` (USB headsets), kernel core sound subsystem.
    - **Devkit + PCM5102A**: stock devkit DTS (`tegra194-p3668-all-p3509-0000.dts`) already enables `tegra_i2s5` with `I2S_DUMMY` codec on the `I2S_DAP` cell via `tegra194-audio-p3668.dtsi` + `tegra186-audio-dai-links.dtsi:1003`. PCM5102A is pin-strap configured (no I2C/SPI control bus), so **no DT overlay or codec node is required** — just turn the SoC audio kernel drivers back on and ALSA exposes card 1 "APE" with 20 ADMAIF PCMs + 2 ADSP FE devices. Header→PCM5102A pinout: 12→BCK, 35→LCK, 40→DIN; SCK→GND, XSMT→3.3V (un-mute — common silence cause), FLT/DEMP→GND. Route ADMAIF to I2S5 in XBAR before playing: `amixer -c APE cset name='I2S5 Mux' 'ADMAIF1'`.
    - **OPE specifically**: lives inside AHUB. Reaching OPE from software requires AHUB + ADMAIF (SW→HUB DMA gateway) + a physical output (I2S or DSPK). HDMI audio uses HDA, separate from AHUB. PipeWire feeds ADMAIF for HUB-routed paths. CS42448 TDM codec work (8out/6in on I2S5) is in `docs/cs42448-devkit-design.md` + related audio docs.
