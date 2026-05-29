# Jetson Xavier NX TRM lookup

Workflow to query NVIDIA's Xavier SoC TRM (8107 pages) without spending
context loading it whole.

## Sources

Both PDFs are committed to the repo at `refs/` (single source of truth — text
extracts below are gitignored, regenerable in <5s):

| File | Pages | Use |
|---|---:|---|
| `refs/Xavier_TRM_DP09253002_v1.4p.pdf` | 8107 | Full SoC TRM — register maps, AHUB topology, pinmux internals, DMA, PCIe, USB phy, etc. |
| `refs/Jetson_Xavier_NX_Pin_and_Function_Names_Guide_v1.0.pdf` | small | Cross-reference for chip pin → module pin → carrier pin |

## Cached extracts (gitignored)

```
.planning/intel/jetson-xnx-trm/
  TRM.txt            # pdftotext -layout, ~22 MB, 8107 \f-delimited pages
  PinGuide.txt       # 17 KB
  .TRM.pageidx       # auto-built line→page index used by trm-lookup.sh
```

Regenerate any time:

```sh
cd refs
pdftotext -layout Xavier_TRM_DP09253002_v1.4p.pdf \
  ../.planning/intel/jetson-xnx-trm/TRM.txt
pdftotext -layout Jetson_Xavier_NX_Pin_and_Function_Names_Guide_v1.0.pdf \
  ../.planning/intel/jetson-xnx-trm/PinGuide.txt
```

`trm-lookup.sh` rebuilds `.TRM.pageidx` automatically if `TRM.txt` is newer.

## Lookup

```sh
LOOKUP=.planning/intel/jetson-xnx-trm/trm-lookup.sh

# Grep + page-number output
$LOOKUP "DAP5_SCLK"           # case-sensitive
$LOOKUP -i "mbdrc"            # case-insensitive
$LOOKUP -p "AHUB"             # group hits by page

# Map between text lines and PDF pages
$LOOKUP --page 3677           # what line range is PDF page 3677?
$LOOKUP --line 228256         # what PDF page contains line 228256?
```

## Drilling deeper

For real register diagrams / pinmux tables, after locating the right
page range with the lookup, use the Read tool on the source PDF with
`pages: "X-Y"` (max 20 pages per call):

```
Read pages: "3677-3681" → §7.8.4.15 Parametric EQ / §7.8.4.16 MBDRC registers
Read pages: "5862-5864" → DAP5 pinmux PADCTL_AUDIO_* registers
Read pages: "2880-2890" → AHUB initialization sequence
```

## Known anchor points (build out over time)

| Topic | PDF page(s) | Notes |
|---|---|---|
| Glossary / abbreviations | p2838 | PEQ, MBDRC, AMX, ADX, etc. |
| AHUB / ADMAIF init sequence | p2885 | Required order for MBDRC+PEQ start |
| PEQ control registers | p3677 (§7.8.4.15) | Parametric EQ hardware accelerator |
| MBDRC control registers | p3680 (§7.8.4.16) | Multiband DRC |
| MBDRC↔PEQ ordering bit | p3677 l228256-7 | Bit 0: data flow direction |
| DAP5_SCLK pinmux | p5862-5863 | PADCTL_AUDIO_DAP5_SCLK_0, CFG2TMC variant |
| OPE1 register base | p38 l2045 | `0x02908200..0x029083ff` (SYSTEM aperture) |

Add new findings to this table as we discover them — avoids re-grepping
the same topics across sessions.
