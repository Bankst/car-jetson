# Remote icecc setup — finish on bankst@10.0.10.45

This box is the **scheduler + a daemon** for the `banks-yocto` icecream net.
Local workstation runs another daemon and routes its jobs here.

Current state when handed off:
- `icecc` package installed.
- `iceccd` running but **without** scheduler/netname flags (stock config in
  `/etc/icecc/icecc.conf` was never patched).
- `icecc-scheduler` service is `inactive (dead)`.

## What needs doing

Patch `/etc/icecc/icecc.conf` so the init scripts pick up the right values,
then restart both services and confirm they bind their ports.

```bash
sudo sed -i \
  -e 's|^ICECC_NETNAME=.*|ICECC_NETNAME="banks-yocto"|' \
  -e 's|^ICECC_MAX_JOBS=.*|ICECC_MAX_JOBS="32"|' \
  -e 's|^ICECC_SCHEDULER_HOST=.*|ICECC_SCHEDULER_HOST="10.0.10.45"|' \
  /etc/icecc/icecc.conf

# Sanity: should print all four lines, no empty values.
grep -E '^ICECC_(NETNAME|MAX_JOBS|SCHEDULER_HOST|ALLOW_REMOTE)' \
  /etc/icecc/icecc.conf

sudo systemctl restart icecc-scheduler iceccd
sleep 2
systemctl is-active icecc-scheduler iceccd     # expect: active / active
ss -tlnp 2>/dev/null | grep -E ':(8765|10245)' # expect: both listening
pgrep -fa icecc                                # confirm -s/-n flags present
```

Expected daemon cmdline (note `-s` and `-n`):
```
/usr/sbin/iceccd -d --nice 5 -s 10.0.10.45 -n banks-yocto -u icecc -b /var/cache/icecc -m 32
```

Expected scheduler cmdline:
```
/usr/sbin/icecc-scheduler -d -l /var/log/icecc_scheduler.log -n banks-yocto
```

## If scheduler still flaps to `inactive (dead)`

The sysv init script is `Type=forking` and silent on errors. Pull the real
reason:

```bash
sudo tail -30 /var/log/icecc_scheduler.log          # scheduler log
sudo journalctl -u icecc-scheduler -n 30 --no-pager # systemd view

# Run in foreground to see the actual error:
sudo -u icecc /usr/sbin/icecc-scheduler -n banks-yocto -vvv
```

Common causes:

- `/var/log/icecc_scheduler.log` not writable by `icecc` user — touch + chown.
- Another scheduler already running on the LAN with the same netname —
  `pgrep -fa scheduler` from another box. Two schedulers per netname is
  invalid; pick one host.
- Port 8765 already bound (`ss -tlnp | grep 8765`).

## Verify from the local workstation

After both services are up, on the local box:

```bash
nc -zv 10.0.10.45 8765   # scheduler
nc -zv 10.0.10.45 10245  # daemon
```

Both should say `succeeded`. If only 10245 succeeds, scheduler still isn't
running — go back to the foreground-run step.

## Firewall

This is an LXC container on Proxmox. No host firewall observed during
investigation. If `nc` fails despite the services listening, check Proxmox
host iptables / pve-firewall rules for the container, not inside the LXC.

## Context — what the local side already has

- `iceccd` running locally with
  `-s 10.0.10.45 -n banks-yocto -m 16` (Debian 13 trixie).
- `kas-icecc:4.7` Docker image built (extends `ghcr.io/siemens/kas/kas:4.7`
  with the `icecc` client).
- `kas/base.yml` carries an opt-in `icecc:` block in `local_conf_header`.
  Activated by `ICECC_DISABLED=` + `--network=host` +
  `ICECC_SCHEDULER_HOST=10.0.10.45` env at build time.

Once the scheduler is up and the two `nc` checks pass, the local side
launches the test build — no further remote action needed.
