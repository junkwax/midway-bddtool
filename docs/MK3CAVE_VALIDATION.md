# MK3CAVE: measured lossless optimization

Validated September 29, 2026 in isolated MK2 build snapshots. The optimized stage
saved **3,096 bytes of packed video data**, while all 69 captured MAME screens
matched the baseline pixel-for-pixel. The live game project was not an output
target. Results describe the captured snapshot; the live project has subsequently
changed independently, so these are not receipts for its latest build.

## Measured costs

| Measure | Before | After | Change |
| --- | ---: | ---: | ---: |
| MK3CV1–4 actual IRW payload bytes | 203,194 | 200,098 | −3,096 |
| Optimizer video estimate | 203,238 | 200,160 | −3,078 |
| Image headers and placement table bytes | 1,144 | 1,576 | +432 |
| Palette bytes | 764 | 764 | 0 |
| Images | 60 | 84 | +24 |
| Stored placements | 64 | 88 | +24 |
| Whole-build bank 1 used bytes | 4,098,922 | 4,095,826 | −3,096 |
| Whole-build bank 1 largest physical gap | 16,940 | 16,940 | 0 |

The linked COFF file also grew by exactly 432 bytes. Program-table growth and
video savings occupy different ROM spaces; do not subtract them into a single
video budget figure. Fixed-size ROM chips themselves do not shrink.

The follow-up native slot-policy capture (`slot-receipts/`) verifies all 90
declared reservations. Bank 1 unused reserved capacity grows from 73,730 to
76,826 bytes; bytes outside reservations remain 21,652, with a largest unreserved
gap of 6,878 bytes. These figures now appear directly in bddtool's receipt view.

The new per-pack free capacity is 12, 28, 34, and 3,044 bytes respectively, inside
the existing declared slots. Most savings accumulate at the end of MK3CV4.
Reassigning that reserved capacity to another asset requires an explicit slot
map change; this validation did not move neighboring assets or slot boundaries.

## Candidate constraints and integration findings

The candidate uses the existing lossless optimizer, with deep search, a maximum
of 24 additional placements, and palette compaction disabled. The entire
`mk3cave3` layer is locked during the scan because its water animation reads rows
directly from packed image data. All seven runtime palette indices stay intact.
Both lossless and shared-family proposals are measured; the smaller proposal is
selected. No deliberate repeat/mirror artwork replacement is included.

Three integration details matter:

1. **Original BDB coordinates must survive export.** This stage's generator moves
   part of the cavern according to its original source X coordinate. Normalizing
   source module shelves can change that predicate. The validation executable
   writes the existing module rectangles and placement coordinates directly and
   checks the saved/reopened source scene.
2. **Trimming can move a runtime module origin.** Trimming the pillar art changes
   `mk3cave2` from 460×186 to 456×185. The generator recrops that module, so its
   world Y must change from 8 to 9 to keep all visible pixels in the same place.
   The first runtime comparison detected this; editor-only reconstruction did not.
3. **The generator's advertised pack limits were stale.** The initial candidate
   fit the generator's old bounds but overflowed current MK3CV2 and MK3CV3 slots,
   overlapping TWRJNT and BLDXPD. `makevrom.py` correctly refused to emit chips.
   The isolated candidate generator now uses the actual `CUSTOM_VIDEO_SLOTS`
   boundaries and measured LOAD2 image lengths for its packing pass. Its
   `MK3CAVE.PACK.json` measurements are tied to the exact BDD SHA-256 and reject
   edited artwork. This is a validated fixed candidate, not a general replacement
   for the game's generator.

The independent runtime checker renders both X and Y flips. The game's existing
Python preview handles only X flips, so it must not be used alone to judge this
candidate's Y-flipped pieces.

## Verification

- Full source scene matches before optimization, after applying the proposal,
  and after saving/reopening the BDB/BDD.
- All four game-generator layers match in world coordinates, including transparent
  margins, overlap order, and both flip axes. Camera rates and floor inputs match.
- LOAD2 output was decoded row by row: 4,640 baseline rows and 5,497 candidate
  rows, with no palette-index truncation or pixel differences.
- All eight water spans rebuilt from their actual packed row offsets. Palette
  order, water parameters, wave data and floor pixels remain identical.
- Both snapshots assembled and linked. The linker reports the same existing
  `ART.OUT` relocation and `.bss` alignment warnings in both builds.
- Both `makevrom.py` builds pass the complete current slot and overlap checks.
- ROM receipts reconstruct and verify all twelve generated video chip lanes.
  Only the four cave IRW payloads change; bank 0 usage is identical.
- MAME reached stage `19` through two-player stage selection. All **69** pairs of
  400×254 RGB captures are identical: stage selection, fight opening, left/middle/
  right camera positions, and 64 consecutive water phases at the middle camera.
- The probe reads the actual `ofree` symbol for the free-object count; the old
  probe's `obj_free` lookup returned −1 and was not a valid RAM measurement.
- All nine native CTest tests pass.

| Runtime sample | Free objects before | Free objects after |
| --- | ---: | ---: |
| Fight opening | 315 | 302 |
| Left camera | 322 | 307 |
| Middle camera | 316 | 301 |
| Right camera | 322 | 314 |

At every sampled stop, both builds report `peak_overload=2`, `qdma_drops=0` and
`cw_short=0`; water line counts also match. These are controlled stage-navigation
and idle-animation checks, not exhaustive combat/fatality stress tests or a
physical arcade hardware test.

## Reproducing the checks

Build the `studio_stage_validation` CMake target. It is a manual integration
utility, separate from CTest because original game assets and DOS tools are not
shipped with bddtool.

```text
studio_stage_validation MK3CAVE.BDB new-candidate-folder
python tests/studio_stage_validation.py before-root after-root new-report.json
studio_stage_validation --compare-rom before-root after-root new-receipt-folder
```

The first command produces a constrained candidate and optimization reports. It
does not make that candidate runtime-ready by itself. The Python check requires
trusted game snapshots with their generators, current pack sidecars, LOAD2 output
and Pillow installed. The receipt command requires generated video chips.
All three commands refuse to overwrite their report destinations.

Local validation artifacts are under `tmp/mk3cave-validation/`:

- `baseline/` and `after/`: complete isolated build inputs/outputs; each contains
  `rom/validation/mk2.zip` and its own `mame-final/` output directory.
- `candidate/`: optimized source BDB/BDD and reports.
- `runtime-packed-validation.json`: current-slot, decoded-pixel and layer checks.
- `receipts/`: before/after receipts and the measured bank comparison.
- `mame-final-comparison.json`: every capture's frame, label and RGB hash, plus
  runtime counters. Corresponding PNGs remain inside the two MAME output folders.
- `snapshot-manifest.json`: captured source file hashes. `source-recheck.json`
  records changes made to the live tree since that capture.
- `candidate-integration.patch`: candidate-specific generator changes, used with
  `after/data/MK3CAVE.PACK.json` and the optimized BDB/BDD.

These assets and ROMs remain local and ignored. Multipart animation export and
reference-proven unused-art removal remain separate work.

## Custom UI export follow-up

MK3CAVE now has a custom **Prepare game export** path in Build & Check. It keeps
original module coordinates, preserves the generator's water source moves,
protects the water layer and seven palettes, and assigns images to the four
current ROM reservations. Apply installs the reviewed pack sidecars, manifest,
generator adapter and build helper with backups. **Build & verify ROMs** runs
the full build and packed-pixel checks before packaging a local ROM ZIP.

The synthetic `studio_cave_profile` CTest checks source moves, water protection,
fractional-scroll placement, save/reopen, stale slot rejection and apply. All
ten CTest tests pass. `studio_cave_export_tests isolated-game new-package
[--apply]` additionally exercises a real lossless candidate when private assets
are available. The UI smoke's existing `--prepare` option covers its review panel.

Follow-up artifacts are under `tmp/cave-ui-validation/`. The native exporter
produced the same 84-image, 88-placement candidate. Independent checks matched
all four runtime layers, all 5,497 packed rows and all eight water spans, with
200,098 total cave video bytes. All 69 emulator captures again matched baseline
exactly (`mame-comparison.json`).

The complete helper run passed LOAD2, assembly/link and cave generation, then
stopped at the snapshot's existing Cage OTOMIX logo consistency check: a frame
list draws a logo without its patch, and JCLOGO.LOD disagrees with the expected
entries. That guard remains enabled. No success receipt was emitted. The emulator
comparison used a separately packaged diagnostic ROM after that failure; it is
evidence for cave parity, not a successful end-to-end helper run. The live game
checkout and emulator installation were not modified.

Later investigation traced those logo-check messages in isolated copies to
missing Git reference history. Supplying the supported `MK2_GIT_DIR` reference
makes the check pass without artwork or guard changes. The [fresh BATTLE/NUPOOL
validation](STAGE_SAVINGS_VALIDATION.md#october-1-fresh-source-full-build-validation)
now includes full baseline/candidate builds through Phase E. It does not replace
the cave helper's own end-to-end validation described above.
