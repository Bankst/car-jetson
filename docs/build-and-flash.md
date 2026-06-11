# Build & Flash

Detailed build/flash procedures for Banks Jetson Linux. Summary + entry points in `CLAUDE.md`.

## One-time host setup

```sh
# Fedora 43 packages required for the flash step (NOT for the build — kas-container is hermetic)
sudo dnf install -y dtc vim-common gdisk bmap-tools cpp lz4

# Docker group access (after install + relogin to load group)
sudo usermod -aG docker "$USER"
# Either log out/back in fully (Plasma session inherits groups at login),
# OR wrap each command in: sg docker -c '...'

# Unattended flash: allow udisksctl mount without password (initrd-flash uses udisksctl)
./scripts/setup-host-flash-permissions.sh
```

## Build

```sh
# Always set both env vars; SELinux escape is mandatory on Fedora enforcing.
KAS_BUILD_DIR=$PWD/build KAS_RUNTIME_ARGS="--security-opt label=disable" \
  kas-container build kas/base.yml          # ~30–60 min cold; minutes warm

# Plasma variant (also rebuild base sstate)
KAS_BUILD_DIR=$PWD/build KAS_RUNTIME_ARGS="--security-opt label=disable" \
  kas-container build kas/plasma.yml        # +1–2 hours cold for KDE
```

## Build with distributed icecc (optional, faster)

Adds remote node `dev-ct` (EPYC 7302P, 32t, Tailscale 100.74.250.91) as
compile farm. Scheduler runs on this Ryzen (100.72.134.17). Aggregate 48t
(16 local + 32 remote). dev-ct is on Tailscale directly — icecc uses
Tailscale IPs throughout.

Prereqs (one-time):
- `icecc-scheduler` running on Ryzen (`systemctl start icecc-scheduler`).
  Listens on port 8765.
- `iceccd` on Ryzen: `ICECC_SCHEDULER_HOST=100.72.134.17`, 16 jobs,
  `ICECC_NETNAME=banks-yocto`. Config: `/etc/icecc/icecc.conf`.
- `iceccd` on dev-ct (100.74.250.91): `ICECC_SCHEDULER_HOST=100.72.134.17`,
  32 jobs, same netname. Config: `/etc/icecc/icecc.conf`.
  No scheduler on dev-ct (`systemctl stop icecc-scheduler` there).
- Verify both registered: `ss -tn 'dport = :8765'` should show two ESTAB
  connections to 100.72.134.17:8765.
- Custom kas image with the icecc client: `docker build -t kas-icecc:4.7
  -f docker/kas-icecc.Dockerfile docker/`.

Invoke:

```sh
KAS_BUILD_DIR=$PWD/build \
KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
  kas-container \
    --runtime-args "--network=host" \
    --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
    build kas/base.yml
```

Two non-obvious wiring details:

1. **`KAS_RUNTIME_ARGS` env var is overwritten** by `kas-container` script
   (line 232) — pass via `--runtime-args` CLI flag instead.
2. **The local iceccd Unix socket must be bind-mounted** into the container
   at `/var/run/icecc/iceccd.socket`. Without this the icecc client falls
   back to building locally (silent regression). The mount is the
   `-v /run/icecc:/var/run/icecc:rw` line.

`--network=host` is required so the local iceccd (talking via the socket)
sees the scheduler on the LAN. Monitor placement with `icemon` on dev-ct, or
`ss -tn '( sport = :10245 )'` on 100.74.250.91 to count live jobs.
See `docker/README.md` for full notes.

**One-time sstate cost**: adding `INHERIT += "icecc"` changes recipe task
signatures across the board, so the first build after enabling will rebuild
most things (≈10 min for `bitbake openssl` + cascade in testing). After
that, the new "icecc-aware" sstate is durable and subsequent builds hit
cache normally.

### icecc required for kernel configure

`INHERIT += "icecc"` in `build/conf/local.conf` — kernel `do_configure` fails without icecc daemon running. Always use `kas-icecc:4.7` container with `--network=host` and `-v /run/icecc:/var/run/icecc:rw` for any build touching linux-tegra. Regular `kas-container shell` without icecc socket mount = kernel configure fails with "unknown assembler invoked".

## Flash

```sh
# Module in recovery: short FC REC pin to GND, power cycle. lsusb should show 0955:7e19.

mkdir -p /tmp/flash
tar xzf build/tmp/deploy/images/jetson-xavier-nx-a203/banks-jetson-image-base-jetson-xavier-nx-a203.rootfs.tegraflash.tar.gz \
    -C /tmp/flash
cd /tmp/flash
sudo ./initrd-flash --erase-nvme            # ~5–15 min, then module reboots automatically
```

After reboot: USB-A203 micro-USB to host PC creates a CDC-NCM ethernet (host gets a `192.168.55.x` IP via the Jetson's NM `shared` DHCP) and a CDC-ACM serial. SSH `ssh root@192.168.55.1` (passwordless via `debug-tweaks` for now).

## Force-rebuild a recipe bypassing sstate

```sh
kas-container shell kas/base.yml -c "bitbake -f -c do_install <recipe> && bitbake -f -c do_package <recipe> && bitbake -f -c do_package_write_deb <recipe>"
```

## Stale lock recovery

Stale `bitbake.lock` / `bitbake.sock` block new builds after unclean container exit. If `kas-container` is Ctrl-C'd or OOM-killed, these files remain in `build/`. The next `kas-container build` will hang waiting for the lock. Fix: `rm -f build/bitbake.lock build/bitbake.sock` before relaunching.

`~/bstat` script tails the most recent `/tmp/r3-build-*.log` with error count (build status without prompting Claude).
