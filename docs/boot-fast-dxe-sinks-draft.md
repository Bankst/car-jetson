# DXE Boot-Time Sink Patches — Draft (Not Yet Applied)

Saved 2026-05-21. Drafted by agent. Apply to `meta-seeed-jetson/recipes-bsp/uefi/edk2-firmware-tegra_%.bbappend` after fresh UART log confirms baseline (current binary built 22:14 post-quieten-patch is untested).

## State of A + draft tasks

| Done | Pending apply | Pending test |
|------|---------------|--------------|
| ATF: `ATF_LOG_LEVEL = "0"` in existing ATF bbappend | sinks 2-4 below | re-flash + UART log delta |
| OP-TEE: new bbappend, sed `CFG_TEE_CORE_LOG_LEVEL,2 → ,0` in `core/arch/arm/plat-tegra/conf.mk` | | |

## Sink 2 — SDHCI retry storm (~50ms)

```
--- a/edk2/MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.h
+++ b/edk2/MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.h
@@ -132,7 +132,7 @@
-#define SD_MMC_TRB_RETRIES  5
+#define SD_MMC_TRB_RETRIES  0
```

```sh
banks_sdhci_no_retries() {
    sed -i 's#define SD_MMC_TRB_RETRIES  5#define SD_MMC_TRB_RETRIES  0#' \
        ${S}/MdeModulePkg/Bus/Pci/SdMmcPciHcDxe/SdMmcPciHcDxe.h
}
```

## Sink 3 — EqosDeviceDxe Mnp wait (~600ms)

```sh
banks_skip_uefi_ethernet() {
    python3 -c '
import re, sys
p = sys.argv[1]
with open(p, "rb") as f: src = f.read()
nl = b"\r\n" if b"\r\n" in src else b"\n"

pat = (
    r"DeviceDiscoveryNotify \(\s*"
    r"IN  NVIDIA_DEVICE_DISCOVERY_PHASES\s+Phase,\s*"
    r"IN  EFI_HANDLE\s+DriverHandle,\s*"
    r"IN  EFI_HANDLE\s+ControllerHandle,\s*"
    r"IN  CONST NVIDIA_DEVICE_TREE_NODE_PROTOCOL\s+\*DeviceTreeNode OPTIONAL\s*"
    r"\)[ \t]*\r?\n[ \t]*\{[ \t]*\r?\n"
).encode()

def sub(m):
    return m.group(0) + b"  return EFI_UNSUPPORTED;" + nl

new, n = re.subn(pat, sub, src, count=1)
if n != 1:
    raise SystemExit("banks_skip_uefi_ethernet: failed to patch EqosDeviceDxe DeviceDiscoveryNotify")
with open(p, "wb") as f: f.write(new)
' ${S}/../edk2-nvidia/Silicon/NVIDIA/Drivers/EqosDeviceDxe/EqosDeviceDxe.c
}
```

## Sink 4 — T194GraphicsOutputDxe head init (~100ms)

```sh
banks_skip_t194_gop() {
    python3 -c '
import re, sys
p = sys.argv[1]
with open(p, "rb") as f: src = f.read()
nl = b"\r\n" if b"\r\n" in src else b"\n"

pat = (
    r"DeviceDiscoveryNotify \(\s*"
    r"IN  CONST NVIDIA_DEVICE_DISCOVERY_PHASES\s+Phase,\s*"
    r"IN  CONST EFI_HANDLE\s+DriverHandle,\s*"
    r"IN  CONST EFI_HANDLE\s+ControllerHandle,\s*"
    r"IN  CONST NVIDIA_DEVICE_TREE_NODE_PROTOCOL\s+\*DeviceTreeNode OPTIONAL\s*"
    r"\)[ \t]*\r?\n[ \t]*\{[ \t]*\r?\n"
).encode()

def sub(m):
    return m.group(0) + b"  return EFI_UNSUPPORTED;" + nl

new, n = re.subn(pat, sub, src, count=1)
if n != 1:
    raise SystemExit("banks_skip_t194_gop: failed to patch T194GraphicsOutputDxe DeviceDiscoveryNotify")
with open(p, "wb") as f: f.write(new)
' ${S}/../edk2-nvidia/Silicon/NVIDIA/Tegra/T194/Drivers/T194GraphicsOutputDxe/T194GraphicsOutputDxe.c
}
```

## Dispatch line addition

```sh
do_unpack[postfuncs] += "banks_dsc_timeout_zero banks_skip_display_gop banks_uefi_quiet \
                        banks_sdhci_no_retries banks_skip_uefi_ethernet banks_skip_t194_gop"
```

## Build

```sh
KAS_BUILD_DIR=$PWD/build \
KAS_CONTAINER_IMAGE=kas-icecc:4.7 \
  kas-container --runtime-args "--network=host" \
  --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
  shell kas/base.yml -c \
  "bitbake -c cleansstate edk2-firmware-tegra && bitbake edk2-firmware-tegra"
```

## Aggregate expected

- Sink 2: ~50 ms
- Sink 3: ~600 ms
- Sink 4: ~100 ms
- Total: **~750 ms** off UEFI DXE phase on top of A (ATF + OP-TEE quietens).

## Test plan summary

1. Flash current binary (built 22:14 post-quieten-patch), capture UART log baseline B0.
2. Rebuild + flash with A (ATF=0 + OP-TEE log=0), capture B1. Compare BL31→UEFI banner gap.
3. Rebuild + flash with sinks 2-4 added, capture B2. Compare UEFI banner→L4TLauncher gap.

Recovery: any UEFI brick → FC REC pin → `sudo ./initrd-flash`. Tegraflash bypasses modified DXE.
