# Current BDD library audit

Follow-up: [isolated BATTLE/NUPOOL validation](STAGE_SAVINGS_VALIDATION.md) measured
4,460 bytes saved for BATTLE and 5,592 for corrected NUPOOL, each with 114 matching
target-stage captures. A combined diagnostic saves 10,054 bytes with 228 matching
captures. Raw output changes pixels in unedited backgrounds. An explicit,
isolated baseline-pixel preservation pass now retains all 479 other static
headers, with the same 10,054-byte saving and 228 matching captures. Sprite
checks report no new changes; 233 inherited undecodable records remain. No
candidate has been installed. The scan estimates below remain the original audit results.

The October 1 refresh reproduced the 10,054-byte saving with full baseline and
candidate builds against a new 5,537-file snapshot, followed by the explicit
preservation pass. All 228 captures and 479 untouched background images still
match. The [remaining-work list](STAGE_SAVINGS_VALIDATION.md#remaining-work)
separates build automation, runtime stress checks and editor features.

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
