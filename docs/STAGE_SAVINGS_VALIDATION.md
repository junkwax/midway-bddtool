# BATTLE and NUPOOL isolated savings validation

The September 2026 experiment used three separate copies under
`tmp/stage-savings-validation/`: `baseline/`, `battle/`, and `nupool/`.
No candidate was applied to the live game or installed into MAME.

| Candidate | Measured MK7 payload saving | Matching MAME captures | Decision |
| --- | ---: | ---: | --- |
| BATTLE, palettes preserved | 4,460 bytes | 114 / 114 | Passes the sampled comparison; retain for review |
| NUPOOL, runtime-order-filtered proposal | 7,720 bytes | 92 / 114 | Reject; rendering differences remain |

## What was checked

- Original source module rectangles and positions were retained. All six layers
  of each final source candidate matched the baseline under X/Y-sorted drawing,
  including both flip axes. Tight module bounds and runtime offsets did not change.
- Palettes were unchanged. BATTLE grows from 30 to 53 images and 59 to 83
  placements; the revised NUPOOL candidate has 58 images and 251 placements,
  versus 52 and 227 in the baseline.
- LOAD2, source promotion, assembly/link, and video-ROM slot/overlap checking ran
  in each copy. MK7MIL payload sizes were 1,545,018 bytes baseline,
  1,540,558 BATTLE, and 1,537,298 NUPOOL.
- Packed image dimensions, BPP capacity, boundaries and decoded rows were checked
  against each BDD. BATTLE's four pre-existing source/packed discrepancies remained
  byte-identical in decoded output. NUPOOL's images 215 and 218 changed their
  decoded pixels despite unchanged source artwork. The verifier records these
  differences and exits nonzero; neither build is described as wholly matching
  its authored BDD pixels.
- MAME selected the target stage through two-player stage selection. Each pair
  includes the VS screen, fight opening, left/middle/right camera stops, 90
  consecutive idle-animation captures, and 19 short combat-sequence captures.
  Fight captures are aligned by frame relative to fight start; absolute frame
  numbers can differ by one. This is not exhaustive combat/fatality testing.
- Sampled BATTLE free-object minimum: 314 baseline, 291 candidate. NUPOOL:
  253 baseline, 242 candidate. Every sample reports zero DMA drops and a peak
  overload value of 2. These are sampled counts, not a complete peak-pool proof.

The original 3,902-byte modeled NUPOOL proposal failed even the sorted-layer
comparison: splitting image 84 changed overlapping pixels. The experimental
`--runtime-order` search rejected that edit and tested each other edit against
the original layers. Its different greedy path modeled 7,714 bytes of savings,
but actual emulator differences still reject that candidate. Source equality
and estimated byte savings alone are insufficient for deployment.

## Build limitations

All three full builds, including the untouched baseline, stopped at the existing
Cage OTOMIX logo consistency check after assembly/link. That check remained
enabled. ROMs used for comparison were separately packaged diagnostics using
`--no-install`, not outputs of a successful end-to-end full build.

Candidate runs used the game's documented `ALLOW_STOCK_BG_DRIFT=1` experimental
setting because deliberately changed BDB/BDD files cannot pass an exact stock
hash guard. The baseline passed that guard normally. No baseline pins or game
guards were edited. This exception does not authorize a production deployment.

## Rollback and source isolation

The original snapshot and baseline ROMs remain beside both candidates. Rejecting
an experiment requires no live rollback: keep using the installed game and leave
the candidate copies unused. Each diagnostic ZIP is inside its copy at
`rom/validation/mk2.zip`; none was installed into `C:/MAME/roms`.

`snapshot-manifest.json` records 5,456 copied source/build files. A final live
source recheck found three newer files: `src/MK1THRN.ASM`, `src/MK1THRNB.ASM`,
and `tools/make_mk1throne.py`. The validation ran only copied scripts rooted in
their isolated directories and did not write those live files. Preserve those
newer changes; do not copy the baseline over the current checkout. Rebase and
revalidate any future integration against a fresh snapshot.

## Evidence and next step

Local ignored evidence includes `comparison.json` (capture hashes and counters),
the four `*-packed-*.json` reports, build logs, candidate optimization reports,
`nupool-source-verification.json`, `live-source-recheck.json`, and each copy's
probe screenshots. `nupool-difference.png` shows one rejected comparison.

Build the `studio_validation_candidate` target to create source-coordinate
experimental pairs in a new folder. Add `--runtime-order` to filter each edit
through sorted-layer reconstruction. This utility is not the UI exporter and
does not produce an installable game package. `verify_static_stage_packing.py`
reads a trusted isolated checkout's helpers and packed MK7 output; it never builds
or writes game files.

Next: investigate NUPOOL's camera-dependent differences and packed-pixel drift,
then add runtime-order and clipping checks to the export gate. BATTLE remains a
promising measured candidate, pending a clean full build and broader stress checks.
