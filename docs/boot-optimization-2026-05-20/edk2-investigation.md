# EDK2 Source Build Investigation — Findings

## TL;DR

**The CLAUDE.md note is outdated.** The source build of `edk2-firmware-tegra`
already works on this repo's current meta-tegra checkout
(`scarthgap-l4t-r35.x`, HEAD commit `0c507bfe`). The `GenFw ERROR 3000 / DOS
header signature not found in UiApp.dll` failure described in CLAUDE.md note #4
has been fixed upstream and the fix is in tree.

Both MACHINE variants have an existing, complete source-build artifact:

- `build/tmp/work/jetson_xavier_nx_a203-poky-linux/edk2-firmware-tegra/35.6.4/build/images/uefi_jetson.bin` (3.2 MB, May 13 19:09)
- `build/tmp/work/jetson_xavier_nx_banks_devkit-poky-linux/edk2-firmware-tegra/35.6.4/build/images/uefi_jetson.bin` (3.2 MB, May 20 12:58)
- `build/tmp/deploy/images/jetson-xavier-nx-banks-devkit/uefi_jetson.bin` (deployed today)

`PREFERRED_PROVIDER_virtual/bootloader = "edk2-firmware-tegra"` is the
default in `meta-tegra/conf/machine/include/tegra-common.inc:31` and **nothing
in this repo overrides it to `tegra-uefi-prebuilt`** (grepped both
`meta-seeed-jetson/conf/` and `kas/`). So the production image is already
booting source-built UEFI.

## Root cause of the historical GenFw failure

Upstream commit `8773a8fd  edk2-firmware-tegra: backport __has_builtin check patch`
(meta-tegra `scarthgap-l4t-r35.x`), which adds:

  `meta-tegra/recipes-bsp/uefi/files/0005-MdePkg-Check-if-compiler-has-__has_builtin-before-tr.patch`

This is a backport of [tianocore/edk2 PR #5781](https://github.com/tianocore/edk2/pull/5781).
The actual chain of failure was:

1. EDK2 builds AARCH64 code with `cpp -undef`. Under GCC 13/14 the `-undef`
   strip undefines `__has_builtin`, so `MdePkg/Include/Base.h` evaluated
   `#elif defined (__has_feature)` after a malformed `__has_builtin(...)`
   expansion → preprocessor error `missing binary operator before token "("`.
2. This caused individual `.obj` builds to either fail or — in some
   configurations — produce truncated / empty `.dll` outputs (PE-COFF inputs
   to GenFw).
3. GenFw, when handed a zero / truncated input, reports it as
   `ERROR 3000: DOS header signature was not found in UiApp.dll` — that
   is GenFw's standard error string for any malformed input PE; it is not a
   GenFw bug and not an LTO bug.

So the original hypothesis in CLAUDE.md ("PE-COFF conversion bug, likely
`-flto` interaction") was wrong. The fix was a 1-line MdePkg header guard,
already shipping in our pinned meta-tegra branch.

## What's actually present in the recipe today

`meta-tegra/recipes-bsp/uefi/edk2-firmware-tegra-35.6.4.inc` applies:

- `0001-Use-bfd-linker.patch` (forces `-fuse-ld=bfd` on the AARCH64 DLINK common
  line — works around a gold-linker `internal error in do_layout` and binutils
  changes)
- `0002-Fix-RCM-boot-detection.patch` (NVIDIA logic fix, unrelated)
- `0003-L4TLauncher-allow-for-empty-missing-APPEND-line-in-e.patch` (unrelated)
- `0004-Ext4Pkg-Advertise-CSUM_SEED-as-supported.patch` (unrelated)
- `0005-MdePkg-Check-if-compiler-has-__has_builtin-before-tr.patch` (THE FIX)

No `-flto` toggling and no GenFw-targeted change was needed. No KCFLAGS-style
suppression list is needed — EDK2 is built standalone (`DEPENDS:remove =
virtual/libc`) and is not subject to poky's default cflags the way the kernel
is.

The recipe `edk2-firmware-tegra_35.6.4.bb` `do_compile:append` consumes the
output correctly: it pulls `FV/UEFI_NS.Fv` and `AARCH64/L4TLauncher.efi`,
catting `nvdisp-init.bin` ahead of the FV payload to make the final
`uefi_jetson.bin`.

## Verification of working build

`build/tmp/work/jetson_xavier_nx_a203-poky-linux/edk2-firmware-tegra/35.6.4/temp/log.do_compile.563932`,
final lines:

```
Generating UEFI_NS FV
######
GUID cross reference file ...
FV Space Information
FVMAIN [99%Full] 32178176 (0x1eb0000) total, 32161432 (0x1eabe98) used, 16744 free
UEFI_NS [99%Full] 2883584 (0x2c0000) total, 2858848 (0x2b9f60) used, 24736 free
- Done -
Build end time: 19:09:53, May.13 2026
Build total time: 00:05:19
Successfully formatted uefi binary to ...uefi_jetson.bin.tmp
Successfully formatted uefi binary to ...BOOTAA64.efi
DEBUG: Shell function do_compile finished
```

Today's devkit build (`jetson_xavier_nx_banks_devkit-poky-linux/.../log.task_order`)
shows `do_compile` finishing at 2026-05-20 16:58, followed by
`do_sign_efi_launcher`, `do_install`, `do_package_write_deb`, `do_deploy`.

## Recommendation for the original goal (patching UEFI to suppress GOP splash)

Since source build works:

1. **No bbappend needed.** Drop the assumption that source build is broken;
   `CLAUDE.md` note #4 should be revised.
2. **Where to patch the splash render loop**: it's in `edk2-nvidia` (the
   NVIDIA platform layer), specifically the `Silicon/NVIDIA/Drivers/NvGopDriver`
   and `Application/UiApp` BDS code path. SRC tree at runtime is
   `${WORKDIR}/edk2-tegra/edk2-nvidia`. Add `file://your-patch.patch;patchdir=../edk2-nvidia`
   entries via a bbappend at
   `meta-seeed-jetson/recipes-bsp/uefi/edk2-firmware-tegra_%.bbappend`.
3. **Iteration loop**:
   - `bitbake-layers add-layer` for meta-seeed-jetson is already done.
   - Edit patch, `bitbake -c cleansstate edk2-firmware-tegra && bitbake edk2-firmware-tegra`.
   - 5-min rebuild (per the log timestamps above).
   - Deployed `uefi_jetson.bin` is picked up by the flash tarball; to apply
     to a live board without full reflash, use the QSPI capsule path or
     `bitbake tegra-uefi-capsules` (recipe already in tree).

## Action items for CLAUDE.md (not done — only investigating)

The "Non-obvious workarounds" item #4 should be updated:

- Remove the "source build broken, using tegra-uefi-prebuilt" claim.
- Note that source build of `edk2-firmware-tegra` works as-is on the pinned
  meta-tegra `scarthgap-l4t-r35.x` HEAD `0c507bfe` thanks to upstream patch
  `0005-MdePkg-Check-if-compiler-has-__has_builtin-before-tr.patch`.
- Mention that the historical `GenFw ERROR 3000` was a downstream symptom of
  a `__has_builtin` macro misbehavior under `cpp -undef` with GCC 13+, not a
  PE-COFF/LTO problem.
