# UEFI splash suppression — investigation notes

## Source trees

- edk2-nvidia: `build/tmp/work/jetson_xavier_nx_banks_devkit-poky-linux/edk2-firmware-tegra/35.6.4/edk2-tegra/edk2-nvidia/`
- edk2:        `build/tmp/work/jetson_xavier_nx_banks_devkit-poky-linux/edk2-firmware-tegra/35.6.4/edk2-tegra/edk2/`
- meta-tegra recipe: `meta-tegra/recipes-bsp/uefi/edk2-firmware-tegra_35.6.4.bb` (+ `-35.6.4.inc`)

Patch dir convention: `patchdir=../edk2-nvidia` (or `../edk2`, `../edk2-platforms`). Files are CRLF.

## Boot timeline (from `scripts/uart-20260520-132015.log`)

| Host monotonic | What |
|---|---|
| +6.08s  | `Jetson UEFI firmware (version v35.6.4 ...)` — first banner from `DisplaySystemAndHotkeyInformation` |
| +6.1s .. +17.0s | ~11s of empty lines, ANSI cursor escapes, `\e[2J\e[4D\e[=3h\e[2J\e[9D` screen-clear at +11.55s |
| +17.15s | Banner reprinted + `ESC to enter Setup / F11 to enter Boot Manager Menu / Enter to continue boot` |
| +17.58s | `L4TLauncher: Attempting Direct Boot` |
| +17.78s | `EFI stub: Booting Linux Kernel...` |

`Timeout` EFI variable is already 0, so the 5s BdsWait countdown is elided. The ~11s gap between the UEFI banner and L4TLauncher is *not* BdsWait — it is `EfiBootManagerConnectAllDefaultConsoles` + `PlatformBootManagerAfterConsole`.

## Root cause of the 11 seconds

In `edk2-nvidia/Silicon/NVIDIA/Library/PlatformBootManagerLib/PlatformBm.c`, function `PlatformBootManagerAfterConsole` (line 1755-1789) runs four blocks in sequence:

```c
BootLogoEnableLogo ();                  // line 1762 — splash bitmap blit + ConOut writes
DisplaySystemAndHotkeyInformation ();   // line 1768 — banner + ESC/F11/Enter prompt (the visible "splash text" on UART)
MemoryTest ();                          // line 1774 — protocol locate + outer TestModes loop even when *Enabled=false
PrintBmcIpAddresses ();                 // line 1777 — IPMI probe (no IPMI on Jetson → fast timeout)
HandleCapsules ();
HandleBootChainUpdate ();
```

The visible cosmetic "splash" (banner reprint + hotkey hints) is purely from `DisplaySystemAndHotkeyInformation`. The wall-clock time before this is also influenced by `EfiBootManagerConnectAllDefaultConsoles` upstream in BdsEntry, which polls USB host controllers and serial drivers. That's outside `PlatformBootManagerLib` and harder to elide cleanly without breaking ConIn for USB keyboards.

T194 GOP (`Silicon/NVIDIA/Tegra/T194/Drivers/T194GraphicsOutputDxe/`) only installs a GOP if NvDisp-Init programmed an enabled+usable head. On this board NvDisp-Init logs `display init failed` (no HDMI cable), so `BootLogoEnableLogo` and the `PrintXY` block guarded by `gST->HandleProtocol(...gEfiGraphicsOutputProtocolGuid...)` short-circuit. The serial-only `Print(Buffer); Print(L"ESC...")` block at lines 1095-1107 still runs.

## Patch chosen

`meta-seeed-jetson/recipes-bsp/uefi/files/0001-banks-suppress-uefi-splash.patch` — comments out the three nuisance calls (`BootLogoEnableLogo`, `DisplaySystemAndHotkeyInformation`, `MemoryTest`). Six-line edit in one function.

Why this and not alternatives:

- **PCD-based disable**: no `PcdShowBootLogo` or `PcdEnableSplash` exists in `edk2-nvidia`. The three calls are unconditional.
- **Drop NvDisplay/T194GraphicsOutputDxe from FDF**: would break HDMI on devkits that *do* have a panel attached, with no benefit on this hw (NvDisp-Init already fails on the A203).
- **Empty `PcdLogoFile`**: no such PCD wired into `LogoDxe` (the logo is published via `gNVIDIAPlatformLogoGuid` and a section in the FFS, not a PCD).
- **`PcdConInConnectOnDemand=TRUE`** would speed up `EfiBootManagerConnectAllDefaultConsoles` by deferring ConIn — but at the cost of physical USB keyboard responsiveness during the boot picker, and the user is OK with serial-only anyway. Deferred to a follow-up if more time savings are needed.

## bbappend

`meta-seeed-jetson/recipes-bsp/uefi/edk2-firmware-tegra_%.bbappend`:

```
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-banks-suppress-uefi-splash.patch;patchdir=../edk2-nvidia"
```

Wildcard `%` matches `35.6.4` (and any future L4T R35.x bump).

## Risk assessment

- **U-Boot SSH-over-USB-NCM during boot**: not affected. SSH-over-NCM is set up *after* kernel boot by `banks-usb-gadget.service`, not by UEFI.
- **ConOut serial output**: not affected. We only comment out three calls inside `PlatformBootManagerAfterConsole`. Subsequent `Print` calls (L4TLauncher status, kernel EFI stub messages, "**WARNING: Test Key is used**", etc.) continue to flow.
- **BootOrder enumeration**: not touched. `PlatformBootManagerBeforeConsole` still runs `EfiBootManagerRefreshAllBootOption` and `SetBootOrder` on first-boot / when `IsPlatformConfigurationNeeded` returns TRUE.
- **GOP**: not touched. If HDMI is plugged in, `T194GraphicsOutputDxe` still installs the GOP at `DeviceDiscoveryDriverBindingStart`. The user just won't see the NVIDIA splash bitmap or the banner overlay — black screen until kernel takes over.
- **Memory test feature**: gone. Users wanting RAM testing must re-enable via UEFI variable + revert patch.
- **Pre-OS hotkey functionality**: F11/ESC still register via `PlatformRegisterOptionsAndKeys` (line 988+) — the hotkeys themselves still work for power users on a serial console. The patch only suppresses the *prompt that tells the user about them*.

## Deploy path

After build, deploy artifact: `build/tmp/deploy/images/<MACHINE>/uefi_jetson.bin`.

UEFI lives on QSPI (or eMMC for non-NX modules). Two options:

1. **Full tegraflash** via `initrd-flash` — works, but overkill: re-flashes everything.
2. **QSPI capsule update** — UEFI itself supports A/B capsule updates of the firmware partition. `meta-tegra/recipes-bsp/uefi/tegra-uefi-capsules_35.6.4.bb` builds the capsule. Drop the capsule onto the running system's ESP and reboot; UEFI consumes it via `HandleCapsules` (this is exactly the path we kept intact in the patch).

Either path requires the user to physically push the new firmware — out of scope for this investigation. The build verification below only confirms `uefi_jetson.bin` is produced.

## Build verification

(See bottom of file — appended after the build run.)

## Build verification — DONE

`bitbake -c cleansstate edk2-firmware-tegra && bitbake edk2-firmware-tegra` exit 0.

Artifact: `build/tmp/deploy/images/jetson-xavier-nx-banks-devkit/uefi_jetson.bin` (3,276,800 bytes, fresh timestamp).

Patch applied cleanly during `do_patch` (no offset, no fuzz).

## Followup: MB2 → "Attempting RCM Boot" gap

The user notes a long delay between MB2 and the L4TLauncher "Attempting Direct Boot / RCM Boot" line. From the current UART log:

- +3.10s: `MB1 done`
- +3.13s: `Welcome to MB2(TBoot-BPMP)`
- +5.05s: OP-TEE / BL31 init begins
- +5.63s: `Welcome to NVDisp-Init` (CPU-BL kernel_boot_app phase)
- +6.08s: Jetson UEFI banner
- +17.58s: `L4TLauncher: Attempting Direct Boot`

So MB2 → UEFI banner is roughly +3.1s to +6.1s = ~3s (BPMP init, ATF/BL31, OP-TEE, NvDisp-Init). UEFI banner → L4TLauncher is the ~11s window the patch above targets.

The bigger MB2 sub-delay is within MB2 itself: 4 separate I2C errors trying to read CVB EEPROM at 0xAE and 0xAC (lines 72-85 of the log). Each EEPROM read times out (slave-not-found path) — that is hardware, not software, since the A203 carrier doesn't populate those EEPROMs. That's a TegraBL behavior baked into MB2 in QSPI; not editable from edk2-firmware-tegra. Would require patching the closed L4T MB2 binary, which is out of scope for an edk2 patch. (NVIDIA ships MB2 source in `tegra-binaries` but the EEPROM-probe retry loop is in `tegrabl_eeprom_manager.c` which we don't currently have in any source tree on this build.)
