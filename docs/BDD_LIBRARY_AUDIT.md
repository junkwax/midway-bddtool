# Current BDD library audit

**October 3 follow-up:** [TOMB testing](TOMB_SAVINGS_VALIDATION.md) measured a
4,512-byte bank increase from the original 566-byte local saving estimate because
an image already shared with other stages was subdivided. Protecting shared
images yields a 174-byte packed saving, but three runtime transition captures
differ. TOMB remains on hold. [MOUNTAIN testing](MOUNTAIN_SAVINGS_VALIDATION.md)
confirms 610 packed bytes saved, but ladder and Yoteigai Tor captures differ and
combat DMA losses increase. It also remains on hold. [SPIRAL's October 4 follow-up](SPIRAL_SAVINGS_VALIDATION.md)
finds a **3,286-byte shared-palette saving** with unchanged placement/palette
counts and linked program size. Its separate preservation diagnostic passes
**1,204/1,204 combat/navigation captures** and 533 unedited static images. The
raw build requires an explicit one-bit preservation step; the earlier 5,348-byte
version remains on hold. Next is a reproducible reviewed job and independent
rebuild of the passing version. Nothing is integrated or installed. Estimates below are
not additive packed savings.

**October 2: work has moved to other stages.** The
[fresh focused audit](NEXT_STAGE_SAVINGS.md) checks MK3CAVE, TOMB, SPIRAL and
MOUNTAIN at zero, four and 24 added placements. The [cave build follow-up](MK3CAVE_VALIDATION.md#october-2-fresh-build-and-combat-validation)
reproduced 3,096 packed bytes saved but found runtime differences; TOMB was tested
subsequently as recorded above.
Most MOUNTAIN proposals fail the stricter static draw-order
verification, so its historical estimates below must not be treated as current
verified opportunities.

The [BATTLE/NUPOOL experiments](STAGE_SAVINGS_VALIDATION.md) remain on hold.
The earlier 10,054-byte result passed limited captures but failed broader runtime
checks. Smaller candidates still show dense round-introduction differences.
NUPOOL's rotating acid bodies are background animations; the user has deferred
further savings work there. No candidate has been installed. The September scan
and totals below are historical, not a list of integration-approved changes.

The September 29, 2026 snapshot of `mk2-main` contains 68 BDD files under
`data/` and `stage_packs/`, including nested imports. All were scanned on frozen
copies. Hashes of 1,538 source/build files matched before and after. No game
build, source save, ROM packing or deployment ran.

The 18 inputs listed directly in the active `MK7MIL.LOD` have **24,672 bytes
(24.1 KiB)** of modeled savings with palettes preserved. Selecting the best
verified proposal per file, including palette copies, raises that estimate to
**76,726 bytes (74.9 KiB)**. These totals are alternatives, not additive, and
are not measured LOAD2 savings. Generated custom packs and their source masters
are reported separately to avoid counting the same artwork twice.

| File | Preserve palettes | Best static-pixel estimate |
| --- | ---: | ---: |
| MOUNTAIN.BDD | 2,406 bytes | 18,042 bytes |
| BATTLE.BDD | 4,448 bytes | 14,068 bytes |
| SPIRAL.BDD | 2,706 bytes | 8,668 bytes |
| FOREST2.BDD | 1,866 bytes | 5,614 bytes |
| TOMB.BDD | 1,200 bytes | 4,856 bytes |
| NUPOOL.BDD | 3,902 bytes | 3,902 bytes |

BATTLE and NUPOOL have now been tested in isolated builds with their palettes
preserved; see the follow-up above. Larger palette proposals need cycling and runtime-budget review:
MOUNTAIN and FOREST2 reach 45 palettes, while BATTLE reaches 44. All six best
proposals above add 24 placements; object/DMA pressure and program-ROM table
growth still require measurement. Forest IMG animations are outside this scan.

Twelve files have at least one unverified mode. Three have no verified
alternative: MK1CAVE has palette-reference/entry mismatches, the generated
MK3CV1 pack has dummy palette assignments unsuitable for a standalone static
color proof, and standalone OUTERHA has no paired BDB. These are marked
incomplete, not counted as zero-opportunity findings. No inputs were repaired.

The existing protected MK3CAVE candidate remains a separate result: 3,078 bytes
in this scan's model versus 3,096 bytes measured in the earlier isolated build.
The scan is bounded, not an exhaustive optimum or a runtime correctness proof.

Local evidence: [all-file report](../tmp/bdd-library-audit-20260929/REPORT.md),
[CSV](../tmp/bdd-library-audit-20260929/summary.csv), and
[full scan results](../tmp/bdd-library-audit-20260929/audit.json).
Private asset copies and detailed reports remain ignored under `tmp/`.

Build the `studio_savings_audit` CMake target, then reproduce with:

```text
python tools/audit_bdd_library.py GAME_CHECKOUT ANALYZER_EXE NEW_REPORT_DIRECTORY
```

Reports must be outside the game checkout. The analyzer only computes proposals;
it never applies one or saves a BDD/BDB. Four scans compare subdivision/reuse
and shared-base/detail decomposition, each with and without palette compaction.
Limits are eight pieces per image, 24 additional placements and 45 palettes.

Remaining product work: an editor-wide library audit view, verified IMG/multipart
animation export, reference-proven unused-art removal, and combat/fatality stress
checks for optimized stage candidates.
