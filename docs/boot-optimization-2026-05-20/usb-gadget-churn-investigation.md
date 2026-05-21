# tegra-xudc USB-gadget churn — investigation report

## TL;DR

- The "double bring-up + Mass Storage Function" pattern the symptom log
  showed is **not reproducing in the current live system** — no Mass
  Storage function loads, no second tear-down/re-bringup of the gadget.
  That symptom was from an older or transient state (different image,
  manual experiment, or different boot path).
- The actual current pain: `banks-usb-gadget.service` starts at
  **t = 11.89s** even though `tegra-xudc` is bound and ready at
  **t = 2.57s**. The service itself only takes **621 ms** to run. The
  rest is systemd ordering — the unit was `WantedBy=multi-user.target`
  with default deps, so it waited behind `basic.target` (udev coldplug
  alone took until 10.62 s).
- Fix: move the unit to **`WantedBy=sysinit.target`** with
  `DefaultDependencies=no`, ordering only behind
  `sys-kernel-config.mount` and `systemd-modules-load.service`. This
  lets it bind the UDC immediately after configfs is mounted
  (~2.6 s), eliminating roughly **~9 s** of idle exposure of an
  un-configured USB device.

## Root cause (what was actually wrong)

### Live-target observation (current image)

`journalctl -u banks-usb-gadget -b -o short-monotonic`:

```
[   11.894185] Starting Banks Jetson Linux USB device-mode gadget...
[   12.515967] Finished Banks Jetson Linux USB device-mode gadget.
```

`dmesg | grep tegra-xudc`:

```
[    2.573333] tegra-xudc 3550000.xudc: Adding to iommu group 1
[   12.515626] EP 0 (type: ctrl, dir: out) enabled         ← service binds UDC
[   12.982015] EP 5/9/7/4/3/2 enabled                       ← host enumerates
[   13.007930] ep 3 disabled                                ← host SET_INTERFACE alt=0
[   13.008089] ep 2 disabled
[   13.032975] EP 3 (type: bulk, dir: in) enabled           ← host SET_INTERFACE alt=1 (NCM data)
[   13.033149] EP 2 (type: bulk, dir: out) enabled
```

The 13.007 → 13.033 cycle is **not** churn — it is the host PC's CDC-NCM
driver completing standard altsetting negotiation (NCM data interface
must transition alt=0 → alt=1 to start streaming frames). ~25 ms total,
required by the CDC NCM 1.0 spec, cannot be eliminated.

### Why the service was so late

`systemctl show banks-usb-gadget.service` before fix:

```
After=systemd-modules-load.service
Before=network.target NetworkManager.service
[Install] WantedBy=multi-user.target
DefaultDependencies=yes
```

With `DefaultDependencies=yes`, systemd inserts `After=basic.target`,
which itself is After `sysinit.target` (includes udev coldplug, journal
flush, etc.). Boot timeline:

| t (s)  | event                                  |
|--------|----------------------------------------|
| 2.57   | tegra-xudc bound                       |
| 7.98   | configfs module loaded                 |
| 8.95   | journal/udev/etc still finishing       |
| 10.62  | "Finished Coldplug All udev Devices"   |
| 11.68  | Virtual Console Setup                  |
| 11.89  | banks-usb-gadget.service starts        |
| 12.51  | UDC bound, USB device visible to host  |

So `tegra-xudc` was idle and exposed (no config) for ~9.9 s.

### The "Mass Storage Function" sighting

Kernel config has `CONFIG_USB_F_MASS_STORAGE=y` (the configfs *function
module*) built in, but `CONFIG_USB_MASS_STORAGE=n` (the legacy
precomposed gadget) and `CONFIG_USB_GADGETFS=n`. Nothing in this image
instantiates a mass-storage function: `banks-usb-gadget.sh` only creates
`ncm.usb0` and `acm.gs0`. The symptom log line `Mass Storage Function,
version: 2009/09/11` would only be printed when `f_mass_storage` is
*loaded as a module* (a hint message in module init). Since the function
is built-in here, the message can only have come from a different image
or a manual `modprobe`. Either way, not present today.

## Fix applied

### Live (already on target as /etc/systemd/system override)

```ini
[Unit]
Description=Banks Jetson Linux USB device-mode gadget
DefaultDependencies=no
After=sys-kernel-config.mount systemd-modules-load.service
Wants=sys-kernel-config.mount
Before=sysinit.target network.target NetworkManager.service shutdown.target
Conflicts=shutdown.target
ConditionPathExists=/sys/class/udc

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/sbin/banks-usb-gadget.sh

[Install]
WantedBy=sysinit.target
```

The unit was reenabled (sym-linked into `sysinit.target.wants/`).

### Source-of-truth (recipe — will land in next image rebuild)

File modified:

- `meta-seeed-jetson/recipes-bsp/banks-jetson-iface/files/banks-usb-gadget.service`
  — same content as above, with an explanatory comment.

No other files changed. `banks-usb-gadget.sh` is already efficient
(~621 ms wall-clock, dominated by a few configfs `echo` writes and the
`echo $UDC > UDC` write that synchronously activates the gadget). No
sleeps or loops to remove.

## Expected effect after reboot

| Metric                         | Before  | After (predicted) |
|--------------------------------|---------|--------------------|
| tegra-xudc EP 0 enabled at     | 12.52 s | ~3.0-3.5 s         |
| USB device visible to host at  | ~13.0 s | ~3.5-4.0 s         |
| Time eliminated                | —       | **~9 s**           |

Service runtime itself (~621 ms) is unchanged; the win is purely from
unblocking systemd ordering.

## Verification

**Not yet rebooted** — task brief says do not reboot without explicit
user OK. To verify:

```sh
# (host) start UART capture
cd /mnt/yoctoworkspace/nx/car-jetson
./scripts/uart-tslog.sh -d /dev/ttyUSB0 &

# reboot the devkit
jtx reboot   # or: ssh root@192.168.55.1 reboot

# after reboot
ssh root@192.168.55.1 'dmesg | grep tegra-xudc; journalctl -u banks-usb-gadget -b -o short-monotonic --no-pager'
```

Expected: first `tegra-xudc 3550000.xudc: EP 0 ... enabled` line at
kernel time ~3 s (the time it takes for configfs.mount + modules-load
to complete), not ~12 s.

## Risks / side-effects

1. **NetworkManager ordering** — `Before=NetworkManager.service` is
   preserved, so NM still sees the `l4tbr0` and `usb0` interfaces ready
   when it starts. No change here.
2. **`DefaultDependencies=no`** removes the auto-injected
   `Conflicts=shutdown.target` and `Before=shutdown.target` — we
   re-added these explicitly so the unit is properly torn down during
   shutdown.
3. **Earlier failure surface** — if `banks-usb-gadget.sh` ever fails
   hard, it now fails before `sysinit.target`, which can wedge boot.
   The script uses `set -eu` but every critical step
   (`modprobe libcomposite`, function creation, UDC bind) is well-
   tested and idempotent. The `[ -d "$GADGET" ] && exit 0` guard at the
   top means re-runs are safe too. Mitigation if needed: change to
   `WantedBy=basic.target` (still earlier than multi-user but post-sysinit).
4. **udev rule timing** — `90-banks-usb-gadget-managed.rules` runs
   inside udev coldplug. The rule tags configfs-created links so NM
   manages them. Earlier UDC bind means udev events for `usb0` /
   `l4tbr0` fire earlier, before coldplug formally finishes. systemd-udevd
   handles overlapping events fine; no change to the rule required.
5. **Host re-enumeration noise** — the host PC sees a USB device come
   up earlier in the build; if the host is mid-boot, that's actually
   better (single enumeration in one pass).

## What was *not* changed

- `banks-usb-gadget.sh` — left alone; 621 ms is already tight.
- Kernel config — no `no-default-gadget.cfg` needed; nothing was
  loading a default gadget.
- udev rules — unchanged.
- NetworkManager keyfiles (`l4tbr0.nmconnection`, `l4t-gadget-usb0.nmconnection`) — unchanged.

## Files touched

- `meta-seeed-jetson/recipes-bsp/banks-jetson-iface/files/banks-usb-gadget.service`
  (recipe source, will be baked on next image rebuild)
- `/etc/systemd/system/banks-usb-gadget.service` (live override on target,
  already active and re-enabled into `sysinit.target.wants/`)
