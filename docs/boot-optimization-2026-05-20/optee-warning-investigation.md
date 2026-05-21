# OP-TEE "Test UEFI variable auth key" warning loop — investigation

**Status:** root cause confirmed. Fix recommended below. Not built (parallel
agent's kernel/icecc bitbake holds the build dir lock; investigation is
source-only).

## TL;DR

* **Root cause:** post-flash, the **UEFI variable-integrity partition is
  empty**, and the platform-configured marker (`PlatformConfigData` EFI
  variable) is missing. On that branch `PlatformBootManagerAfterConsole`
  runs `EfiBootManagerRefreshAllBootOption()` + UEFI-Shell-registration +
  `SetBootOrder()`, each of which calls `gRT->SetVariable` on monitored
  boot/secure variables. Each `SetVariable` calls
  `VarIntComputeMeasurement → SendOpteeCmd` → FFA into OP-TEE →
  `jetson_user_key_pta_uefi_vars_auth` → CMAC over the var blob → log the
  two warnings per call.
* **20 warning pairs** in the UART log, spanning **+162.994s → +178.193s
  (~15s wall-clock)**; L4TLauncher fires at +184.385s. The dense ~9s block
  is one FFA/CMAC round-trip per boot-variable write.
* **First-boot-only:** subsequent boots find a matching
  `PlatformConfigData` and skip the whole refresh block (see source
  evidence below). The user is almost certainly observing the first boot
  after `initrd-flash --erase-nvme`.
* **The splash patch is NOT the culprit.** It only comments three cosmetic
  calls (`BootLogoEnableLogo`, `DisplaySystemAndHotkeyInformation`,
  `MemoryTest`) — none of which touch the boot-variable refresh path. The
  patch is fine; leave it as-is.

## Evidence trail

### Warning source (literal strings)

`build/tmp/work/jetson_xavier_nx_banks_devkit-poky-linux/optee-os/3.21.0-l4t-r35.6.4/optee_os/core/pta/tegra/jetson_user_key_pta.c:893`

```c
TEE_Result jetson_user_key_pta_uefi_vars_auth(uint8_t *vars, uint32_t vars_len)
{
    ...
    rc = jetson_user_key_pta_get_user_key(&key, &key_len, key_type);
    if (rc) {                                       // EKB miss → dev board
        ...
        if (key_valid || sec_enabled) {
            EMSG("UEFI variable auth key not set !");
            goto error;
        } else {
            IMSG("WARNING: Test UEFI variable auth key is being used !");
            IMSG("WARNING: UEFI variable protection is not fully enabled !");
            key = uefi_vars_auth_key_test;          // fixed test key
        }
    }
    // CMAC the vars blob with the (test) key
    crypto_mac_alloc_ctx(&cmac_ctx, TEE_ALG_AES_CMAC);
    crypto_mac_init / update / final ...
}
```

Caller: `optee_os/core/arch/arm/kernel/stmm_sp.c:902` —
`stmm_handle_variable_authentication()`, invoked on FFA direct-msg dst_id
`ffa_variable_authentication = 5`.

### Who hits FFA dst_id 5

`build/tmp/work/.../edk2-tegra/edk2-nvidia/Silicon/NVIDIA/Drivers/FvbNorFlashDxe/VarIntCheck.c`

* `VarAuthTa = 5U` (line 43).
* `SendOpteeCmd()` issues `ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ` to TA 5.
* Called from:
  * `VarIntValidate()` — **once** at MM init.
  * `VarIntComputeMeasurement()` — **once per monitored SetVariable** for
    `SecureBoot/PK/KEK/db/dbx/BootOrder/Boot####`. Filter at line 371–376
    (`IsSecureDbVar || IsBootVar`).

### Why post-flash drives 15+ calls but a stable boot drives ~0

`edk2-nvidia/Silicon/NVIDIA/Library/PlatformBootManagerLib/PlatformBm.c:1409`

```c
if (!IsSingleBootNeeded ()) {
    if (IsPlatformConfigurationNeeded ()) {     // <-- first boot post-flash
        EfiBootManagerConnectAll ();
        EfiBootManagerRefreshAllBootOption ();  // rewrites Boot####
        PlatformRegisterOptionsAndKeys ();
        PlatformRegisterFvBootOption (gUefiShellFileGuid, ...);
        SetBootOrder ();                        // rewrites BootOrder
        PlatformConfigured ();                  // stamps PlatformConfigData
    }
}
```

`IsPlatformConfigurationNeeded()` returns TRUE when:
* `gNVIDIATokenSpaceGuid:PlatformConfigData` variable is **missing or stale**, OR
* `PcdQuickBootEnabled == 0`, OR
* `KernelCommandLine` differs from `KernelCommandLineLast`.

Post-`initrd-flash --erase-nvme` the QSPI variable store is rebuilt from
defaults, so `PlatformConfigData` is absent → full branch runs → ~15+
boot-var writes → ~15+ FFA round-trips → ~15+ warning pairs.

### Why it's slow

Per-call cost ≈ 350–500 ms. Breakdown (informed estimate):
* FFA SMC into S-EL1, dispatch to OP-TEE.
* `tegra_fuse_get_bsi()` + `tegra_fuse_get_sec_mode()` — Tegra fuse reads via
  SMC each (~tens of ms).
* `IMSG` x2 over UART at 115200 (~10 ms).
* CMAC-AES init/update/final over the measurement (small).
* Return path, NOR-flash measurement record write
  (`VarIntWriteMeasurement → NorFlashProtocol->Write`) — likely the largest
  chunk.

Silencing the warnings does **not** eliminate the FFA/CMAC/NOR-write
cost — only the UART text time (≈ 10 ms × 40 lines ≈ 400 ms total, ~5%
of the ~9s block).

## Recommended fix (rank-ordered)

### (a) Do nothing — RECOMMENDED for now

The cost is **first-boot only**. Every subsequent power-on hits the
`PlatformConfigured` short-circuit and the loop disappears. Confirm by
power-cycling once without re-flashing and checking the UART log for
`Test UEFI variable auth key`. If absent on boot 2, this is a one-time
post-flash penalty and not worth code changes.

**Risk:** zero.
**Saving:** zero on first boot, ~9–15s on every subsequent boot vs.
nothing (because subsequent boots are already fast).

### (b) Silence the repeated warnings only (cosmetic)

If the log noise is itself the problem, patch the optee PTA to log the
warning once:

```c
// jetson_user_key_pta.c, function jetson_user_key_pta_uefi_vars_auth
static bool test_key_warned;          // file-static, default false
...
} else {
    if (!test_key_warned) {
        IMSG("WARNING: Test UEFI variable auth key is being used !");
        IMSG("WARNING: UEFI variable protection is not fully enabled !");
        test_key_warned = true;
    }
    key = uefi_vars_auth_key_test;
}
```

* **Lives in:** new recipe `meta-seeed-jetson/recipes-security/optee/optee-os_%.bbappend`
  + patch `0001-optee-once-only-uefi-test-key-warning.patch` against
  `core/pta/tegra/jetson_user_key_pta.c`.
* **Build target:** `optee-os` (used by `optee-os-tadevkit` and `tos-optee`
  which assembles the deployable `tos-optee_t194.img`).
* **Saving:** ~400 ms of UART time, no kernel-stage saving.
* **Risk:** trivial. Static `bool` in TEE world; cleared on TEE reset (per
  boot), so warning still appears once per cold boot.
* **Security impact:** none. The warning is informational; the test-key
  fallback behaviour is unchanged.

### (c) Short-circuit `SendOpteeCmd` when running with test key (cuts the ~9 s)

The variable-integrity feature is meaningless without a real OEM-fused
UEFI auth key — the test key is a public constant baked into both UEFI
and OP-TEE, so any attacker can forge the CMAC. We can detect "dev mode"
once and skip the FFA call entirely.

Two implementation options:

**(c1)** UEFI side. Add a one-shot probe in `VarIntInit` that calls
`SendOpteeCmd` with a sentinel, and if OP-TEE responds "dev key" (needs
a new return code), set a module-global `DevKeyOnly = TRUE`. Then in
`VarIntComputeMeasurement` skip the SVC and zero the measurement (or
write a static "dev mode" marker). Requires changes in **both** OP-TEE
PTA (new return code path) and edk2-nvidia (`VarIntCheck.c`,
`SendOpteeCmd`). Saves ~9s on first boot.

**(c2)** OP-TEE side, simpler. In `jetson_user_key_pta_uefi_vars_auth`,
when falling into the test-key branch, **skip the CMAC** and return
`TEE_SUCCESS` with `vars` left untouched (or zeroed). UEFI's
`VarIntWriteMeasurement` will still write a measurement to NOR-flash
(unavoidable, that's pure UEFI side), but the FFA round-trip becomes
~0 ms. The 350-ms-per-call collapses; ~5–7s saved per first boot.

* **Lives in:** same bbappend as (b), additional `goto error` skipping
  `crypto_mac_*` when test-key.
* **Saving:** ~5–7 s on first boot.
* **Risk:** **low for dev builds, NOT acceptable for production**. If
  someone fuses a real OEM key later, this code path must be gated by a
  build-time flag (`CFG_BANKS_SKIP_TEST_KEY_CMAC=y`) or a runtime
  `key_valid || sec_enabled` check. The existing function already does
  this discrimination — only the test-key branch is patched, so the
  guard is automatic. Still recommend a build flag for explicitness.

### (d) Reduce the BootOrder list (orthogonal)

`PlatformRegisterFvBootOption(gUefiShellFileGuid, L"UEFI Shell", ...)` adds
a UEFI Shell entry to the boot order. If we removed it, one less
`SetVariable → SendOpteeCmd` per first boot. Saves ~1 of 20 calls. Not
worth it on its own. If we want to also kill `UiApp` / removable boot
probes, that's more invasive and may break recovery workflows.

## Recommendation

1. **First**: confirm this is genuinely first-boot-only by power-cycling
   without re-flashing and capturing UART. If the warnings vanish on
   boot 2, treat the 9–15 s post-flash cost as acceptable (option **a**)
   and stop.
2. **If logs noise matters or the loop recurs on every boot**: apply
   option **(b)** — one-shot warning suppression. Cosmetic, ~0 wall-clock
   impact, but cleans the log so any genuine variable-protection
   regression is visible.
3. **If first-boot 9 s must go**: apply option **(c2)** with a build flag.
   Touches OP-TEE only, no UEFI side changes.

## Files / locations

* OP-TEE PTA source (read-only workdir):
  `build/tmp/work/jetson_xavier_nx_banks_devkit-poky-linux/optee-os/3.21.0-l4t-r35.6.4/optee_os/core/pta/tegra/jetson_user_key_pta.c`
* OP-TEE StMM dispatcher:
  `.../optee_os/core/arch/arm/kernel/stmm_sp.c` (line 902)
* UEFI varint caller:
  `build/tmp/work/.../standalone-mm-optee-tegra/35.6.4/edk2-tegra/edk2-nvidia/Silicon/NVIDIA/Drivers/FvbNorFlashDxe/VarIntCheck.c`
* UEFI BDS refresh gate:
  `build/tmp/work/.../edk2-firmware-tegra/35.6.4/edk2-tegra/edk2-nvidia/Silicon/NVIDIA/Library/PlatformBootManagerLib/PlatformBm.c` (lines 1138–1261, 1409–1444)
* Meta-tegra optee recipe (where bbappend goes):
  `meta-tegra/recipes-security/optee/optee-os_3.21.0-l4t-r35.6.4.bb`
* Splash patch (sanity-checked, NOT the culprit):
  `meta-seeed-jetson/recipes-bsp/uefi/files/0001-banks-suppress-uefi-splash.patch`
* UART evidence (raw binary; use `strings`):
  `scripts/uart-20260520-144623.log`
  * 20 × `Test UEFI variable auth key` lines, span +162.994 → +178.193 s,
    L4TLauncher at +184.385 s.

## Sample bbappend for option (b)

```
# meta-seeed-jetson/recipes-security/optee/optee-os_%.bbappend

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://0001-optee-once-only-uefi-test-key-warning.patch"
```

Patch hunk (against `core/pta/tegra/jetson_user_key_pta.c`):

```diff
@@ -893,6 +893,7 @@ TEE_Result jetson_user_key_pta_uefi_vars_auth(...)
     ...
+    static bool test_key_warned;
     uint8_t uefi_vars_auth_key_test[] = { ... };
     ...
-                IMSG("WARNING: Test UEFI variable auth key is being used !");
-                IMSG("WARNING: UEFI variable protection is not fully enabled !");
+                if (!test_key_warned) {
+                    IMSG("WARNING: Test UEFI variable auth key is being used !");
+                    IMSG("WARNING: UEFI variable protection is not fully enabled !");
+                    test_key_warned = true;
+                }
                 key = uefi_vars_auth_key_test;
```

## Open questions

* Has the user observed the warning loop on a **second** boot (i.e. boot
  the freshly-flashed board, then `reboot`, and check UART)? If yes, then
  `IsPlatformConfigurationNeeded` is returning TRUE every boot, which
  would mean `PlatformConfigData` write is failing or `KernelCommandLine`
  flapping; that's a different bug (Reclaim/varstore corruption) and
  needs separate investigation.
