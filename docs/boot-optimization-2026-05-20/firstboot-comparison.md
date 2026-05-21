# First-boot / Initrd Architecture: OXOS MC2 reference vs car-jetson

Reference repo: `/home/bankst/microc/dev/mcx-monorepo/mcx-infrastructure/mc2-yocto-os`
Stack: **Yocto kirkstone-era** (meta-tegra `kirkstone-l4t-r35.x` branch judging by the meta-tegra `tegra-flash-init` interface that already exposes `init-extra.d/`), same `meta-tegra` BSP family we use. Distros: `oxos-mc2-emitter` and `oxos-mc2-cassette`. Same `initrd-flash` tooling, same `tegra-minimal-init` boot initramfs. **This is directly comparable to our build** — not a Debian-base detour.

## Architecture summary (one paragraph)

OXOS's central pattern is **flash-time provisioning via meta-tegra's `init-extra.d/` hook**, run from inside the `tegra-flash-init` initrd that `initrd-flash` boots on the target. The host-side `initrd-flash` script stages per-device material (SSH pubkey, Mender preauth key, Tailscale preauth, hostname, flash timestamp) into a directory baked into the flash payload at `/tmp/flashpkg/flashpkg/conf/`; on the target, after partitions are written and before reboot, a manifest-driven runner (`init-extra.sh`) executes numbered scripts (`05-`, `20-`, `30-`, `40-`, `50-`, `55-`, `60-`) that each mount the freshly-formatted `/dev/nvme0n1p16` (their `/data` partition), drop their files into it, unmount, and return. The target's production rootfs then boots into a system where `/data` is already populated — there is **no on-target first-boot provisioning step** at all for keys, certs, identity, or device flags. The only true runtime services are per-boot helpers (`rtc-hctosys`, `save-good-time`, `setup-swapfile` which is idempotent), plus mode-selection targets gated on `/data` sentinel files (`ConditionPathExists=/data/production`).

## File-by-file (relevant only)

### Production initramfs (boot path)
- `meta-tegra/recipes-core/initrdscripts/tegra-minimal-init_1.0.bb` — upstream tiny initramfs, mounts root + `switch_root` to `/sbin/preinit`.
- `meta-oxos-mc2/recipes-microc/tegra-minimal-init/tegra-minimal-init_%.bbappend` — replaces upstream `init-boot.sh`.
- `meta-oxos-mc2/recipes-microc/tegra-minimal-init/custom/init-boot.sh` — adds `efivarfs` mount; sources optional `/etc/platform-preboot` and `/etc/platform-pre-switchroot` rootfs hooks for last-mile customization without touching initrd recipe. No first-boot-style work here.

### Flashing initramfs (the heart of the pattern)
- `meta-tegra/recipes-core/initrdscripts/tegra-flash-init_1.0.bb` — upstream: provides `init-flash.sh` which runs `command_sequence` (bootloader, erase-nvme, partition, then calls `./init-extra-pre-wipe` and `./init-extra` if present).
- `meta-tegra/recipes-core/initrdscripts/tegra-flash-init/init-extra.sh` — upstream default runner (just executes scripts in `/init-extra.d/`).
- `meta-oxos-mc2/recipes-microc/microc/custom/init-extra.sh` — **OXOS override**: manifest-driven runner. Only scripts listed in `/init-extra.d/manifest` execute; sorted-numeric order; logs each step.
- `meta-oxos-mc2/recipes-microc/microc/tegra-flash-init_%.bbappend` — installs `init-extra.sh`, `05-fix-partition-table.sh`, `55-stamp-flash-time.sh`, appends both to manifest.

### `init-extra.d/` scripts (run **once at flash**, inside initrd, after partitions written, before reboot)
| Script | Recipe | What it does |
|---|---|---|
| `05-fix-partition-table.sh` | `microc/tegra-flash-init` bbappend | `blockdev --rereadpt /dev/nvme0n1` so kernel sees fresh GPT |
| `10-setup-hostname.sh` | `meta-oxos-mc2-cassette/.../hostname/tegra-flash-init` | writes `/data/config/hostname` from staged conf |
| `20-setup-mender.sh` | `meta-oxos-mc2/recipes-mender/mender-preauth/tegra-flash-init` | copies `mender-agent.pem` (per-device, generated host-side) into `/data` |
| `30-setup-tailscale.sh` | `recipes-tailscale/tailscale-preauth/tegra-flash-init` | writes Tailscale preauth key into `/data` |
| `40-setup-authorized-keys.sh` | `recipes-fips/openssh/tegra-flash-init` | drops production SSH `authorized_keys` into `/data` |
| `50-setup-flags.sh` | `microc/tegra-flash-init` (in microc_1.0.bb dir) | `touch /data/microcdebug` + `/data/production` (mode flags) |
| `55-stamp-flash-time.sh` | `microc/tegra-flash-init` | copies host `flash-time` (epoch at flash) into `/data/flash-time` for `rtc-time-checker` floor |
| `60-setup-wifi-pw-and-mac.sh` | `microc-cassette/tegra-flash-init` | writes WiFi cred + MAC into `/data` |

### Host-side staging (runs on dev workstation, just before initrd-flash hands payload to target)
- `meta-oxos-mc2/recipes-microc/mc2-initrd-flash/custom/initrd-flash.sh` — patched `initrd-flash`. Lines 367–396 create `flashpkg/conf/`, drop `mender-agent.pem`, `pubkey.txt`, `tailscale-preauth-key.txt`, `hostname`, `flash-time`, and a `command_sequence` that includes `extra-pre-wipe` and `extra` phases.
- `meta-oxos-mc2/recipes-microc/mc2-initrd-flash/custom/initrd-flash-oxos.sh` — wrapper: runs `./keygen-client` (Mender), `./create-ssh-keypair`, then parallel-flashes all attached Jetsons. Each device gets distinct per-device material.
- `tegra-helper-scripts-native` bbappends (4 of them) — install host-side companion scripts (`keygen-client`, `create-ssh-keypair`, `tailscale-preauth-client`) into the SDK so `initrd-flash` can call them.

### Production rootfs services (every-boot or oneshot true-first-mount-needed)
- `microc_1.0.bb` enables: `toggle-ssh-password-auth.service`, `setup-swapfile.service`, `set-hostname.service`, `log-cleanup.service/.timer`, `rtc-hctosys.service`, `save-good-time.service`. All idempotent. None gate root-fs setup or partition init.
- `setup-swapfile.sh` — idempotent: `swapon --show=NAME | grep -q $SWAPFILE && exit 0`. Wanted by `multi-user.target` (runs late, doesn't block boot graph). This is the closest equivalent to our `banks-persist-setup`.
- `production.target` / `standard.target` — user-level systemd targets with `ConditionPathExists=/data/production` / `!`-form — mode-select gating, **no service work**.
- **No** `firstboot.target`, no `ConditionFirstBoot=`, no marker-stamp services. The whole pattern is "flash already populated /data; rootfs trusts it".

### Overlayfs for writable state
- `microc_1.0.bb` `inherit overlayfs`; declares `OVERLAYFS_WRITABLE_PATHS[data] += "/var/log /opt/oxos/data/config/device-specific /home/microc/persist"`. This is meta-virtualization's `overlayfs.bbclass` — it generates systemd mount/overlay units at build time, so per-boot overlay setup is **declarative**, not a custom shell service.

### NVIDIA boot/OTA cruft
- **Not masked, not patched, not removed** in OXOS. `nv_update_verifier`, `setup-nv-boot-control`, `nvpmodel`, `nvphs`, `nvstartup`, `nvfancontrol` all ship as meta-tegra defaults. No bbappend touches them. (Reading: OXOS accepts the ~1–2s these add and focuses optimization energy elsewhere.)

## What runs WHERE — table

| Operation | OXOS | car-jetson today |
|---|---|---|
| Re-read partition table after format | **flash-initrd** (`05-fix-partition-table`) | n/a (no equivalent) |
| Format `/data` partition | done by meta-tegra `init-flash.sh` per partition spec | **first-boot rootfs** (`banks-persist-setup`) |
| Mount `/data` for permanent fstab | declarative via fstab + overlayfs.bbclass | **first-boot rootfs** (banks-persist-setup) |
| SSH `authorized_keys` install | **flash-initrd** (per-device pubkey) | **first-boot rootfs** (bind-mount from baked-in keys) |
| Mender / OTA agent identity | **flash-initrd** (per-device PEM) | n/a |
| Tailscale preauth | **flash-initrd** | n/a |
| Hostname | **flash-initrd** (`/data/config/hostname`) | static in image |
| WiFi creds / MAC | **flash-initrd** | n/a |
| Mode flags (debug/production) | **flash-initrd** (touch /data sentinels) | n/a |
| Flash timestamp for RTC sanity floor | **flash-initrd** | n/a |
| EFI Timeout var | n/a (they accept default) | **first-boot rootfs** (`efi-timeout.service`) |
| `nv_update_verifier` | every-boot (accepted cost) | every-boot |
| `setup-nv-boot-control` | every-boot (accepted) | every-boot |
| BT keys / SSH key persistence | covered by `/data` provisioning at flash | **first-boot bind-mount** in `banks-persist-setup` |
| RTC time floor | every-boot `rtc-hctosys.service` (Type=oneshot, Before=sysinit) | n/a |
| Swap setup | every-boot, idempotent fast-path | n/a |
| `/var/log` & config writable layers | systemd overlayfs mount units (declarative, fast) | n/a |

## Recommendations for car-jetson

### R1 — Move `banks-persist-setup` partition format + `/data` provisioning into a flash-time `init-extra.d/` script
**Mechanism**: Add `meta-seeed-jetson/recipes-bsp/banks-persist-flash/tegra-flash-init_%.bbappend` that installs `50-format-uda.sh` and appends `50-format-uda` to `/init-extra.d/manifest`. Script mounts the UDA partition node from inside the flash initrd, runs `mkfs.ext4` (or whatever fs), and pre-creates the directory layout (`ssh/`, `bluetooth/`, etc.). On the production rootfs, replace the `banks-persist-setup.service` with a static `/etc/fstab` entry — kernel mounts `/data` during normal `local-fs.target` and no oneshot is needed.
**Boot saving**: 1–3s (eliminate the entire service and its dependents). Bind-mounts become symlinks or `Bind=`/`BindPaths=` in service units.

### R2 — Move SSH authorized_keys onto `/data` populated at flash, drop the bind-mount oneshot
**Mechanism**: `recipes-connectivity/openssh-keys/tegra-flash-init_%.bbappend` installing `40-setup-authorized-keys` — copies a host-staged `pubkey.txt` to `/data/ssh/authorized_keys`. Then sshd_config gets `AuthorizedKeysFile /data/ssh/authorized_keys`. No bind-mount, no boot-time logic.
**Boot saving**: small (~50–200ms), mainly removes ordering complexity in the persist setup chain.

### R3 — Move `efi-timeout` to a flash-time write of the EFI var inside `/init-extra.d/`
**Mechanism**: `recipes-bsp/efi-timeout/tegra-flash-init_%.bbappend` installing `15-set-efi-timeout` — script mounts efivarfs from inside the flash initrd (it's already available — `init-boot.sh` mounts it; in flash initrd, just do `mount -t efivarfs efivarfs /sys/firmware/efi/efivars`), `printf '\x07\x00\x00\x00\x00\x00' > /sys/firmware/efi/efivars/Timeout-...` with the right attributes, done. No stamp file, no service. NB: this writes to QSPI which is **not erased** by `--erase-nvme`, so it persists across rootfs reflashes.
**Boot saving**: 0.3–0.8s (removes `efi-timeout.service` entirely + its sysinit-target ordering).

### R4 — Adopt OXOS's `platform-preboot` / `platform-pre-switchroot` hook pattern in our `tegra-minimal-init` bbappend
**Mechanism**: copy OXOS's `init-boot.sh` verbatim into `meta-seeed-jetson/recipes-core/initrdscripts/tegra-minimal-init_%.bbappend` + `custom/init-boot.sh`. This is a no-cost upgrade: future per-boot one-shots that must happen before rootfs init (e.g. waiting on a slow NVMe to enumerate, sourcing CAN HW ID into kernel cmdline, A/B slot selection) can land as `/etc/platform-preboot` files in regular rootfs recipes without re-touching the initrd recipe.
**Boot saving**: enables future R5/R6/etc. without recipe churn.

### R5 — Replace ad-hoc `tmpfiles.d` / udev-settling boot graph with overlayfs.bbclass for any `/var/log`-style writable state
**Mechanism**: `inherit overlayfs` in a small recipe; declare `OVERLAYFS_WRITABLE_PATHS[data] += "/var/log"` etc. systemd generates mount units at build time, mount happens in parallel with rest of `local-fs.target`.
**Boot saving**: cosmetic per-boot (~100–300ms) but big maintainability win — no more service ordering bugs.

### R6 — Don't bother stripping `nv_update_verifier` / `setup-nv-boot-control`
OXOS keeps these. Their cost is real but they're load-bearing for A/B updates and SMD signature verification on Tegra. If we're not running Mender or A/B yet, masking them is fine but the saving is ~1s, not 5s. **Lower priority than R1–R3.**

### R7 — Host-side keygen + per-device material staging script (model after OXOS's `initrd-flash-oxos.sh`)
**Mechanism**: small `scripts/setup-host-flash-permissions.sh` companion — `scripts/stage-device-material.sh` that generates per-device hostname, drops user's SSH pubkey, optional cellular ICCID, etc. into `flashpkg/conf/` before invoking `initrd-flash`. Read by R1/R2/R3 init-extra scripts.
**Boot saving**: enables per-device identity from flash time onward; eliminates need for any "image yourself on first boot" identity step.

### Estimated cumulative boot savings (kernel-handoff → multi-user.target)
- R1 (drop banks-persist-setup): **1–3s**
- R2 (authorized_keys via flash): 0.1–0.2s
- R3 (efi-timeout via flash): 0.3–0.8s
- R5 (overlayfs declarative): 0.1–0.3s
- **Total realistic: 1.5–4s off every boot**, all by moving one-shots out of the rootfs critical path and into the flash-time initrd that runs once per device lifetime (or per reflash).

## Notes / caveats

- OXOS's `meta-tegra` is on the same `kirkstone-l4t-r35.x` family interface; their `tegra-flash-init` recipe is byte-identical to ours upstream. The `init-extra.d/` + `init-extra-pre-wipe.d/` hooks are present in our tree (`/mnt/yoctoworkspace/nx/car-jetson/meta-tegra/recipes-core/initrdscripts/tegra-flash-init/`). **No upstream patch is required to adopt R1–R3.**
- The OXOS pattern requires that `--erase-nvme` is **not** the default flash mode in steady state (or that material is re-staged each flash). For us, where we routinely `--erase-nvme`, this is fine — the staging step is in the wrapper script.
- OXOS uses `/dev/nvme0n1p16` as `/data` (separate from rootfs `/dev/nvme0n1p1`). We'd want a parallel UDA layout — likely add a separate partition in `flash.xml` substitution rather than reusing rootfs.
- The `init-extra-pre-wipe.d/` hook (runs **before** partition table is wiped) is also available. Useful for capturing prior-flash state (e.g. dumping old `/data/flash-time` to keep RTC monotonic across reflashes). OXOS doesn't currently use it, but it's there.
- OXOS does **not** ship any patches against `nv_update_verifier` / `nvstartup` / `nvfancontrol`. They simply accept the cost. We should benchmark before spending time on these.
