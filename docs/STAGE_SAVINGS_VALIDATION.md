# BATTLE and NUPOOL isolated savings validation

**Current priority: move to other stages.** NUPOOL's bodies that fall into the
acid and change each round are background animations. At the user's request,
NUPOOL savings work and the combined BATTLE/NUPOOL investigation are deferred.
The [fresh next-stage audit](NEXT_STAGE_SAVINGS.md) covers MK3CAVE, TOMB, SPIRAL
and MOUNTAIN. The [fresh cave validation](MK3CAVE_VALIDATION.md#october-2-fresh-build-and-combat-validation)
reproduced its saving but found combat/transition differences. The
[TOMB follow-up](TOMB_SAVINGS_VALIDATION.md) saves 174 packed bytes after retaining
cross-stage sharing, but still differs in three round-redraw captures. The
[MOUNTAIN follow-up](MOUNTAIN_SAVINGS_VALIDATION.md) confirms 610 packed bytes
saved but finds ladder/Peak rendering differences and higher combat DMA losses.
It is also on hold. The [October 4 SPIRAL follow-up](SPIRAL_SAVINGS_VALIDATION.md)
finds a **3,286-byte shared-palette saving** with no added placements, palettes
or linked program size. Its isolated preservation diagnostic passes
**1,204/1,204 combat/navigation captures** and all 533 unedited static images.
The raw build needs an explicit one-bit baseline-pixel preservation step.
The earlier 5,348-byte candidate and smaller added-palette version remain on
hold. Next is a reproducible reviewed job and independent rebuild of the passing
shared-palette version. Nothing is integrated or installed. This changes the
work order, not the validation status of the experiments below.

**October 2 status: the combined candidate remains on hold after broader testing.**
Reducing subdivisions removes the measured pressure increases, but dense capture
still finds round-introduction differences—even in a zero-added-placement control.
The earlier 10,054-byte candidate remains rejected. Nothing has been installed
into the live game. See [the reduced-cost experiments](#october-2-reducing-subdivision-cost)
for the latest results and why placement count alone is insufficient.

September 2026 experiments use separate copies under
`tmp/stage-savings-validation/`. No candidate was applied to the live game or
installed into MAME. The raw candidates change pixels in unedited backgrounds.
The October 1 follow-up adds an explicit baseline-pixel preservation artifact:
**10,054 bytes saved**, all **228 target-stage captures matching**, and all
**479 image headers in 16 unedited static tables matching**. This remains an
isolated experiment; the preservation pass is not part of the normal UI/build.

A fresh October 1 snapshot now reproduces this result through **complete baseline
and candidate builds, including LOAD2**, followed by the explicit preservation
pass and diagnostic packaging. Both builds pass through Phase E. The final
comparison again has 228/228 matching captures and 479 unchanged background
images. See [fresh-source validation](#october-1-fresh-source-full-build-validation)
for the scope, artifacts and remaining work.

| Candidate | Measured MK7 payload saving | Matching target-stage MAME captures | Bank-wide decision |
| --- | ---: | ---: | --- |
| BATTLE, palettes preserved | 4,460 bytes | 114 / 114 | Hold: NUENT1/NUENT2 pixels change |
| NUPOOL, original X/Y-filtered proposal | 7,720 bytes | 92 / 114 | Reject: target-stage rendering changes |
| NUPOOL, corrected Z/Y checks and shared artwork retained | 5,592 bytes | 114 / 114 | Hold: NUENT1/NUENT2 and MK1PIT pixels change |
| BATTLE + corrected NUPOOL together | 10,054 bytes | 228 / 228 | Raw output changes NUENT1/NUENT2 pixels |
| Combined + explicit baseline-pixel preservation | 10,054 bytes | 228 / 228 | Graphics checks pass; isolated review artifact |

Savings are measured against a 1,545,018-byte baseline MK7 payload. The combined
payload is 1,534,964 bytes. Its saving differs from adding the two standalone
measurements, which is why combined builds need their own receipt.

## October 1: baseline appearance preserved

`getcksum` in the archived `doc/load2/ldbgnd2.c` uses `while (--ct)` and skips
the last input word when calculating both the checksum and maximum color index.
`load_bits` in `load2.c` then ORs the pixel value into a 16-bit word without
masking it to BPP. An oversized pixel can therefore affect the next pixel;
whether that extra bit survives depends on the record's bit alignment. A
synthetic regression reproduces image 33's 9-to-8 change at different offsets.
Recompiling/fixing that legacy packer would change inherited rendering and is a
separate project from preserving the current game appearance.

`tools/preserve_stage_pixels.py` creates a new diagnostic IRW from an explicitly
selected baseline and candidate. It restores only reviewed, uncompressed
background records. For this combined build, the two NUENT header references
share one payload: preserving their bottom-right pixel changes **one physical
payload bit**, plus the IRW checksum. The payload size, addresses, table records,
authored BDB/BDD files, and all edited-stage decoded pixels remain unchanged.
The reference appearance includes pre-existing defects; this is not an authored
pixel repair and is not a change to the LOAD2 executable.

The pass requires unchanged source pairs for the approved records, compatible
headers, agreement between every alias, at most 16 changed pixels per approved
record, and no overlapping parsed sprite reference. It checks all 570 candidate
sprite reference spans in this bank conservatively. It refuses unapproved or
partial background aliases, compressed repairs, source changes, changed geometry,
conflicting baseline expectations, and unsupported IRW layouts. It rechecks all
unedited static tables and ensures the edited tables were not altered. Output
must be a new directory outside both input checkouts. The JSON receipt pins input,
source, header and output hashes, pixel coordinates, bit offsets and byte savings.

The resulting `combined-preserved/` diagnostic passes:

- 479 unedited static image headers across 16 tables, decoded against baseline.
- 228 target-stage MAME captures, with the prior object/DMA results unchanged.
- The existing sprite identity audit: 10,247 records, zero changed pixels,
  additions, removals or duplicate labels. Of these, 233 were undecodable before
  and remain undecodable; they are not claimed as pixel-verified. One sprite
  record relocates with identical decoded pixels.
- Ten new preservation tests and the four packed-verifier tests. Cases include
  shared pixels, adjacent bit protection, conflicting/partial/unapproved aliases,
  flat bank-one sprite addresses, source drift, excessive changes, idempotence,
  refusal to overwrite output, checksums and unchanged input files.

After correcting the scratch-copy dependencies, both builds were reassembled,
passed their checks through Phase E, and were repackaged with `--no-install`.
The program binaries changed, so the final `baseline-checked/` versus
`combined-checked/` probes were repeated: **228/228 captures still match**.
The IRW and BGNDTBL inputs remained byte-identical to the tested preservation
artifact. All 268 IRW/TBL files used by the sprite audit are unchanged in each
copy after the build continuation, so its decoded-pixel results still apply.

A fresh LOAD2 run overwrites the repaired IRW. Repeat the reviewed preservation
pass and all relevant checks after a rebuild. No automatic production hook or
editor Apply action has been added.

## NUPOOL findings and corrected candidate

The first validation helper incorrectly treated X/Y order as runtime draw order.
`src/MKDISP.ASM`'s `insobj_v` actually inserts by Z, then Y, with new equal-key
objects before existing ones. Subdivision changes a piece's Y and can therefore
change overlaps. The helper now compares original and proposed RGB555 layers
under Z/Y sorting and both directions of the X/record tie order. This is a
conservative static screen, not an exhaustive camera-arrival simulation.

`src/BAKGND.ASM`'s `bsrch1stxb` also requires block tables sorted by X. The helper
now sorts exported BDB records and resets their order fields. An isolated
sorting-only experiment still had the original 22 capture differences; sorting
was necessary but did not resolve the observed rendering problem by itself.

LOAD2 has a separate unaligned-width hazard. `doc/load2/ldbgnd2.c` reads tightly
packed BDD pixels, while `zcom.c`'s analysis advances over four-pixel row strides.
For some unaligned images, this reads stale buffer contents from earlier images.
Removing NUPOOL image 213 changed the packed pixels of unchanged images 215 and
218. The helper conservatively retains preceding images whose buffer contents
can influence such reads. The final candidate's three pre-existing authored vs.
packed discrepancies are identical to the baseline's decoded pixels.

The first corrected Z/Y candidate passed 114 captures but saved only 842 bytes.
Image 87 already shared its ROM address with other stage header tables; splitting
it lost that reuse. The final search preserves shared images 36, 39, 87 and 90.
It accepts changes to images 21, 84, 30, 96, 57 and 221, models 5,586 bytes and
measures 5,592 bytes saved. It has 60 images and 251 placements, versus 52 and
227 originally. All six module bounds and all nine palettes are unchanged.
Regenerating the candidate with the refactored helper produced identical BDB/BDD
bytes. These exclusions are evidence from this bank snapshot, not universal IDs.

## Why the wider bank check still blocks integration

The read-only verifier now compares decoded pixels in every unedited static
background header table, ignoring harmless ROM address relocation. It checks
17 other tables for a single-stage edit and 16 for the combined edit. Separate
IMG/multipart tables are outside this check's scope.

| Build | Unedited background images with changed packed pixels |
| --- | --- |
| BATTLE only | NUENT1 and NUENT2 image 51: one pixel each |
| NUPOOL only | NUENT1/NUENT2 images 42 and 54: one pixel each; MK1PIT image 201: one pixel |
| Combined | NUENT1 and NUENT2 image 33: one pixel each |

The NUENT sources contain indices exceeding their generated BPP capacity:
image 51 has index 18 in 3 BPP, images 42/54 have index 18 in 4 BPP, and image 33
has index 17 in 4 BPP. The source artwork is unchanged. Packed results vary as
preceding allocations move. For example, combined image 33 changes its bottom
right pixel from index 9 to 8. The decoded source mismatch already exists in
the baseline; optimizing another stage exposes a different packed result.
MK1PIT image 201's first pixel also changes in the NUPOOL-only build; its exact
packing interaction still needs investigation.

Do not silently repair authored images, pin new baselines, or approve the edited
stages alone. First isolate and resolve the packing defects in a separate copy,
then compare the complete affected bank again. A clean static comparison also
does not replace runtime, palette, animation, or combat testing.

## Validation coverage and limitations

- Each stage's 114 captures include VS, fight opening, left/middle/right camera
  stops, 90 consecutive idle-animation frames and 19 short combat captures.
  Captures align by frame relative to fight start, not absolute emulator frame.
- Sampled free-object minimum is 314 baseline / 291 BATTLE and 253 baseline /
  242 final NUPOOL. The combined probes have the same candidate minima. Samples
  report zero DMA drops and peak overload 2. These are not exhaustive pool or
  fatality stress proofs.
- BATTLE's four inherited authored/packed discrepancies and final NUPOOL's three
  retain their baseline decoded hashes, including in the combined build.
  Source-pixel verification still returns nonzero rather than hiding them.
- LOAD2, source promotion, assembly/link and ROM slot/overlap checks ran inside
  each copy. The initial runs stopped at the Cage OTOMIX check because scratch
  copies lacked Git history. Supplying its supported `MK2_GIT_DIR` reference to
  the original read-only history makes that check pass without altering art or
  the guard. The retail palette check also requires the sibling readonly tree;
  79 ASM and 166 IMG reference files were copied into the isolated reference
  area with hash manifests. Both `baseline-checked/` and `combined-checked/`
  complete `build.py --asm-only` through Phase E with exit 0. This continues
  the previously generated LOAD2 artifacts; a fresh all-in-one build with the
  preservation hook is not implemented. These setup omissions were not defects
  in the Cage artwork.
- Comparison ROMs were separately packaged diagnostics using `--no-install`.
  Candidate builds used documented `ALLOW_STOCK_BG_DRIFT=1` because edited BDDs
  cannot pass the stock hash guard. No source pins or game guards were changed.
- Thirteen CTest checks, four Python verifier tests and ten preservation tests pass. Synthetic regression
  cases cover Z/Y overlap changes, tie order, flips, transparency, clipping,
  unaligned buffer context, cross-stage sharing, bit decoding and detection of
  a relocated image with a changed pixel.

## Reproduction and evidence

Build `studio_validation_candidate`, then create the final NUPOOL source pair in
an unused output folder (image IDs below are decimal):

```text
studio_validation_candidate baseline/data/NUPOOL.BDB NEW_FOLDER --runtime-order --exclude-image=36 --exclude-image=39 --exclude-image=87 --exclude-image=90
```

This manual utility preserves original source coordinates. It is not the UI
exporter and does not produce an installable game package. Static runtime-order
checks are now integrated into the optimizer and normal export gate, as described below.

The packed verifier reads a trusted isolated checkout; it neither builds nor
writes game files. Reports must be new files:

```text
python tools/verify_static_stage_packing.py GAME_COPY NUPOOL NEW_REPORT.json
python tools/verify_static_stage_packing.py --compare-bank BASELINE_COPY CANDIDATE_COPY NEW_BANK_REPORT.json OLHDRS leHDRS
python -B -m unittest discover -s tests -p test_static_stage_packing.py
python -B -m unittest discover -s tests -p test_preserve_stage_pixels.py
```

`OLHDRS` and `leHDRS` explicitly exclude the edited NUPOOL/BATTLE header tables
from the second command; validate those separately. An unchanged-stage pixel
difference returns failure. Reports also list shared ROM addresses, so a local
optimization can be reviewed for lost cross-stage reuse.

Create the preservation artifact separately from both input copies:

```text
python tools/preserve_stage_pixels.py BASELINE_COPY COMBINED_COPY NEW_PRESERVATION_FOLDER --keep NUENT1:T1HDRS:10 --keep NUENT2:T2HDRS:12 --edited OLHDRS --edited leHDRS
```

The approved table indices are specific to this frozen snapshot. The command
writes a new `MK7MIL.IRW` and `preservation.json`; it does not replace candidate
files or install ROMs. For this experiment, the result was copied into a new
isolated checkout and diagnostic ROMs were repackaged with `--no-install`.

Ignored local evidence includes `nupool-followup-comparison.json`,
`combined-capture-comparison.json`, `*-bank-comparison.json`,
`cross-stage-packing-differences.json`, `*-packed-*.json`,
`nupool-final-source-verification.json`, build logs and probe screenshots.
The original rejected NUPOOL experiments remain available for comparison.
Follow-up evidence includes `preserved-bank/preservation.json`,
`combined-preserved-bank-comparison.json`, `combined-preserved-capture-comparison.json`,
`combined-preserved-sprite-comparison.json`, `*-logo-git-check.log` and the
readonly-reference hash manifests. Final evidence is in
`checked-build-bank-comparison.json`, `checked-build-capture-comparison.json`,
`checked-build-artifact-comparison.json`, `checked-sprite-inputs.json` and
`*-checked-references-build.log`. `preserved-bank-reproduced/` was regenerated
with the final utility and contains the same IRW plus exact-byte header hashes.

## Rollback and isolation

`baseline/`, `battle/`, `nupool-final/` and `combined/` each retain their own source
and diagnostic ZIP at `rom/validation/mk2.zip`. Rejecting them requires no live
rollback: leave those copies unused and keep using the installed game.

`snapshot-manifest.json` records 5,456 source/build files. The previous live
recheck found newer `src/MK1THRN.ASM`, `src/MK1THRNB.ASM` and
`tools/make_mk1throne.py` files outside these experiments. Preserve live changes;
never copy the old baseline over the current checkout. The follow-up recheck is
saved separately in `followup-live-source-recheck.json`; it now lists 100 changed
files, including additional source, tooling and build outputs. The live checkout
has continued moving independently of these frozen experiments. Any future
integration needs a fresh snapshot and revalidation.

## Editor artwork checks

The editor now captures decoded MK7 background fingerprints in version 3 ROM
receipts. Preparing an export captures the existing build as a baseline when
none is present; successful editor-launched builds capture the result. Both
Build & Check and ROM receipts show unchanged-source regressions separately from
byte savings, while edited stages remain marked for visual review. Older
receipts load with artwork coverage explicitly unavailable.

Native comparisons of these frozen copies reproduced both outcomes: the raw
combined build has 477 matching images and two regressions (NUENT1/NUENT2);
the checked, preserved build has 479 matching images and no regressions. Both
have two edited stages requiring visual review. Hidden-window UI smoke runs
confirmed both comparison states, with the regression warning above the budget
table. These checks read the existing artifacts without building or installing
game files. The native decoder tests also cover bit-address relocation, malformed
payloads, source changes and receipt persistence.

## Editor runtime-order checks

The lossless verifier now rejects proposals that change per-layer RGB555 output
under MKDISP's Z/Y insertion priority, testing both X arrival directions for ties.
The check runs again at Apply, including shared-base and pattern-packing proposals.
Ordinary and cave game exports sort their isolated source copies by X within each
layer, reopen them and compare against the edited document. A failed or unavailable
roundtrip prevents package preparation; the existing file-integrity checks pin
successful packages through Apply. Normal authoring saves keep their existing order.

The real rejected NUPOOL copy reproduces 718 changed pixel samples in DPUL5 across
the two modeled arrival orders. The corrected `nupool-final/` copy has zero changes
across all six layers. Native tests cover a split that reconstructs perfectly but
changes overlap priority, explicit Z priority, flips, opaque black, hidden objects,
custom source offsets, repacked origins, and sorting with sidecar flags retained.
An isolated MK3CAVE optimization/export also passes all four layers with zero
changes. No ROM build or live installation was run for this editor change.

These checks are bounded static models, not an emulator or proof of every camera
history. Export roundtrips compare the edited scene with its serialized form;
intentional edits still need review against the previous game build.

## October 1: fresh-source full-build validation

The refreshed input snapshot contains 5,537 files from `mk2-main`, captured at
Git HEAD `54047571b045bf2a36fc3a54fb9d900c6216bf29` plus the working tree's
uncommitted content. SHA256 manifests, rather than Git HEAD alone, identify the
actual inputs. Compared with the previous snapshot, 124 inventoried files had
changed and 81 were added. The BATTLE/NUPOOL source pairs still match exactly,
so the previously reviewed candidate pairs were reused without other source edits.

`refresh-20261001-source/` retains the input snapshot. Independent
`refresh-20261001-baseline/` and `refresh-20261001-candidate/` copies both ran
`build.py` with LOAD2 enabled and passed through Phase E. The baseline passed
the stock background guard normally; only the intentional candidate used
`ALLOW_STOCK_BG_DRIFT=1`. No source pins or guards were changed. The historical
Git reference for the Cage logo check was supplied through `MK2_GIT_DIR`, and
all 327 isolated retail-reference files matched their authoritative copies.
An initial scratch setup lacked nested RAINPSX input artwork; after including
the nested source assets, the complete baseline build was rerun successfully.

Raw candidate packing still reproduces the two NUENT1/NUENT2 differences.
`refresh-20261001-preservation/` contains a new receipt and preservation artifact
from these fresh outputs: one physical payload bit plus the checksum changes.
`refresh-20261001-preserved/` is a separate copy with that artifact installed.
Program/video packaging and `mamerom.py --no-install` succeeded for all three
build copies. The preservation pass follows the full candidate build; it is
still an explicit compatibility operation, not an automatic build hook or
a repair of LOAD2 itself.

Final results:

- MK7 payload: 1,545,018 → 1,534,964 bytes, saving **10,054 bytes**.
- Native receipt capture verifies all twelve video chips and reports **479
  unchanged images**, no unedited-stage regressions, and two edited stages for review.
- NUPOOL **114/114** and BATTLE **114/114** full RGB emulator captures match.
  Coverage includes selection, opening, camera travel, idle animation and short combat.
- Sampled free-object minima remain 253 → 242 for NUPOOL and 314 → 291 for BATTLE;
  both candidates have zero sampled DMA drops and a peak overload of 2. This does
  not replace long combat/fatality stress testing.
- Sprite audit: 10,268 records, no changes/additions/removals or duplicate labels;
  one relocation retains identical pixels. The same 233 inherited undecodable
  records remain outside decoded-pixel coverage.
- The final read-only recheck matches **all 5,537 captured live inputs**. No live
  source files or installed ROMs were replaced.

Evidence under `tmp/stage-savings-validation/` uses the `refresh-20261001-` prefix:
`manifest.json`, `candidate-sources.json`, `*-full-build.log`, `raw-bank.json`,
`preservation/preservation.json`, `preserved-bank.json`, `native-artwork.txt`,
`captures.json`, `sprite-comparison.json`, `live-recheck.json`, `artifacts.json`
and `summary.json`. Each build copy retains its diagnostic `rom/validation/mk2.zip`.
Rejecting the experiment still requires no live-file rollback.

## Remaining work

**Active next step:** package SPIRAL's passing 3,286-byte shared-palette
diagnostic as a reproducible reviewed transformation/build job, including its
explicit preservation artifact, and independently rebuild it from pinned inputs.
See [SPIRAL's results](SPIRAL_SAVINGS_VALIDATION.md). The BATTLE/NUPOOL transition investigation
below is retained for a future return, not the next task.

1. **Build workflow follow-up.** Prepared MK7 preservation jobs now run from the
   editor with pinned inputs, final chip/artwork checks and no-install packaging
   (see below). Initial job preparation remains on the command line. A future
   preparation UI and support for other candidate types can build on this path.
   Deterministic LOAD2 repair and authored-art corrections remain separate projects.
2. **Deferred: resolve transition drawing differences.** The BATTLE screen-edge regression
   is resolved. Smaller candidates eliminate the measured pressure increases,
   but dense capture still exposes round-introduction differences, including in
   a zero-added-placement control. Isolate background reload and drawing timing
   around `MAIN.ASM:play_1_round` / `do_a11_background` and the queue reset in
   `MKDISP.ASM:init_dma_regs`; no engine fix has been made. Placement limits alone
   do not establish equivalence. Rerun the dense checks before live integration.
   More matchups, active attack sequences and camera histories still remain.
3. **Library-wide savings view.** Bring the existing read-only batch audit into the
   editor, distinguish source masters from generated packs, and rerun estimates
   through the stricter runtime-order verifier. The September estimates are historical.
4. **Verified animation export.** Extend IMG/multipart optimization through packing
   and runtime validation, including Forest face animation and palette behavior.
5. **Reference-proven unused-art removal.** Expand the existing audit into reviewed
   removal with dynamic/code references accounted for and an undo/rollback path.

No candidate has been promoted or installed. LOAD2 buffer-context and bit-overflow
behavior remain separate from the static runtime-order model.

## Reviewed rebuild workflow

The editor now has **Build & Check → Reviewed rebuild** for prepared MK7 jobs.
See [Reviewed stage rebuilds](REVIEWED_STAGE_BUILDS.md) for preparation and use.
Jobs pin the baseline, raw candidate, retail reference and verification tools;
each run creates independent scratch copies and repeats the full build,
explicitly reviewed preservation, chip/artwork checks, strict sprite comparison
and no-install packaging. Only a fully checked run receives `SUCCESS.json`.
Initial job preparation remains a command-line operation.

The first end-to-end run exposed a separate packaging defect: the existing
CRC fallback recognized zero-filled bytes inside `SKSOUL4.IRW` as padding and
changed 16 occupied video bytes. Raw chips and decoded-art checks passed, but
the new ZIP check rejected the package. This does not establish visible sprite
corruption, and previous BATTLE/NUPOOL screenshots did not cover all affected
artwork. The rejected run remains at `tmp/reviewed-stage-run/`, without a success
receipt.

The adapter now supplies the existing packager with checksum patch locations
outside every verified video payload, records the offsets, and verifies both
occupied bytes and emulator CRC targets after packaging. This adjustment is
limited to the isolated packaging process; the live `crc_spoof.py` and game
sources remain unchanged. Native and Python regressions cover process arguments,
stale input/tool rejection, failed-build evidence, source preservation and the
zero-filled-artwork packaging case.

The corrected job completed from fresh copies at
`tmp/reviewed-stage-run-safe-crc/`, using
`tmp/reviewed-stage-job-safe-crc/job.json`. Its full build passed through Phase E;
raw MK7/header hashes and the one-bit preservation receipt reproduced exactly.
Final checks report **10,054 bytes saved**, **479 unchanged background images**,
zero unedited-stage regressions, and the same 10,268 sprite records with no pixel
changes/additions/removals. The ZIP changes 48 video padding bytes and **zero
occupied video bytes**, and all twelve video CRCs match the emulator targets.
The original input inventories still match their pinned hashes.

The new ZIP was also run through the two existing emulator probes: NUPOOL
114/114 and BATTLE 114/114 full RGB captures match the fresh-source baseline.
Sampled free-object minima remain 242 and 291, respectively; both have zero
sampled DMA drops and peak overload 2. `emulator-captures.json` records these
comparisons alongside `SUCCESS.json`, step logs, ROM receipts, the raw MK7
backup and preservation artifact. The 233 inherited undecodable sprite records
and longer combat/effects/fatality coverage remain limitations.

Validation: Release build, all 13 native CTests, 11 reviewed-build Python tests
(plus the existing 14 preservation/packing tests), and the editor UI smoke pass.
No candidate or packaging-script change was installed into the live game.

## October 2: extended runtime validation

`tools/stage_runtime_validation.py` now runs the game's existing controlled Lua
probes against independently selected baseline/candidate ROMs. It hashes the
ROMs, emulator, maps and probe inputs, directs logs/captures/emulator state into
a new output folder, and compares full RGB screenshots. Added metrics sample
the free-object list, DMA drops, overload and stage identity on every fighting
frame. Timeouts, missing success markers, incorrect stages, broken/exhausted
object lists, changed pixels, DMA drops or increased peak overload prevent a
passing report. Capture timing is compared relative to the fighting captures;
a constant boot-frame offset is allowed.

The completed six-case run is `tmp/runtime-stress-20261002-final/`. It compares
the October 1 baseline ZIP with `tmp/reviewed-stage-run-safe-crc/rom/mk2.zip`.

| Controlled scenario | Matching RGB captures | Minimum free objects, baseline → candidate | Result |
| --- | ---: | ---: | --- |
| NUPOOL, repeated Raiden lightning and uppercuts | 240 / 240 | 228 → 214 | Pass |
| NUPOOL, Baraka decapitation | 43 / 43 | 269 → 253 | Pass |
| BATTLE, repeated Raiden lightning and uppercuts | **167 / 240** | 301 → 278 | **Fail: right-edge pixels** |
| BATTLE, Baraka decapitation | 43 / 43 | 317 → 294 | Pass |
| NUPOOL stage fatality, victim x=310, attacker on right | 42 / 42 | 237 → 224 | Pass |
| NUPOOL stage fatality, victim x=830, attacker on left | 45 / 45 | 246 → 232 | Pass |

Both combat cases measure 7,203 fighting frames, exercise three camera positions
twice, and observe 79 lightning specials plus 40 uppercuts per build. Baraka's
probe confirms the fatality and follow-through pose; the Dead Pool probe observes
the body fall, acid splash and skeleton after triggering the stage fatality.
All six candidates have zero measured DMA drops, peak overload 2 (same as
baseline), correct stage identity and no broken free-object chains.

The BATTLE mismatches contain 65–96 changed pixels per affected capture, all
within the rightmost three screen columns (x=397..399). The first is
`lightning-2/*/screens/mk2/0043.png`; `lightning-2/diff-0043.png` shows the
baseline, candidate and RGB difference. This points to a screen-edge behavior
that the whole-layer static compositor and previous camera stops did not cover.
At this point the exact cause had not been established; the follow-up below
isolates it to placement-row order. No pixel tolerance or cropped comparison
was used to pass it. The overall `summary.json` reports `passed: false`, and
`FAILED.txt` retains the failure. The prior build receipt remains a record of
packing verification, not a runtime approval.

These probes set fighter selection, health, timer and/or positions in emulator
RAM. They cover selected effects, camera positions and fatalities, not normal
match pacing, all camera histories, all fighters or long tournament sessions.
Combat snapshots occur every 30 measured frames, with five-frame sampling while
the death flag is active; the stage-fatality probe also takes event snapshots.
The same inherited undecodable sprite coverage limitations still apply.
Early launcher trials that did not reach a complete probe are retained separately
and are not counted as passing evidence. Four harness regression tests pass,
covering timing alignment, pixel/pressure failures, incomplete captures and
unexpected probe versions. Live game sources and installed ROMs remain untouched.

To run all current cases from the tool workspace (Pillow and the local game's
named Lua probes are required; the runner now includes four input-driven cases):

```powershell
python -B tools/stage_runtime_validation.py BASELINE_CHECKOUT CANDIDATE_CHECKOUT BASELINE_ROM_DIR/mk2.zip CANDIDATE_ROM_DIR/mk2.zip MAME_EXE NEW_OUTPUT_FOLDER
```

Use repeated `--scenario` options to narrow a run, for example
`--scenario lightning-2`. Every run requires a new output folder. Review
`inputs.json`, `summary.json`, each case's `comparison.json`, and the retained
screenshots/logs before accepting a candidate. The failed candidate above remains
rejected; the separate corrected candidate and its evidence follow.

## October 2: BATTLE placement-order correction

Runtime tracing found unsorted X rows in the experimental BATTLE source's BAT4
and BAT7 modules. In BAT4 a mirrored split emitted X=687 before X=627. LOAD2
preserves this row order, while `BAKGND.ASM`'s `disp_add` uses `bsrch1stxb` and
stops scanning beyond the viewport. At the failing camera positions the visible
rock piece was skipped. The missing pixels were a placement lookup failure;
the packed artwork itself was unchanged.

The new `edge-20261002-candidate/` copy under `tmp/stage-savings-validation/`
changes only BATTLE.BDB row order, stably sorting X then Y. Row contents, module
bounds, palettes and BATTLE.BDD remain unchanged. A full build through Phase E
produces the same raw MK7 payload and decoded image headers as the prior raw
candidate. Placement tables change as intended. All 5,540 inventoried files in
the prior candidate still match their original hashes.

The reviewed build adapter now checks edited modules for nondecreasing X order
when preparing a job, before a run creates scratch copies, and after building.
It rejects the old candidate and accepts the corrected BATTLE/NUPOOL pair.
Existing editor game exports already sort placements; this guard also covers
externally prepared jobs. It refuses invalid input rather than silently changing
the reviewed source. A regression test covers the mirrored split, equal-X rows
and independent module origins. All 30 Python tests pass across reviewed builds,
preservation, packing and runtime validation, and the updated adapter is bundled
beside the editor.

The new pinned job, `tmp/battle-edge-reviewed-job/job.json`, completed at
`tmp/battle-edge-reviewed-run/`. Its `SUCCESS.json` records:

- **10,054 bytes saved**, with the same explicit one-bit preservation correction.
- All twelve video chips verified; **479 unchanged background images**, zero
  unedited-stage regressions, and two edited stages requiring runtime review.
- 10,268 sprite records, zero changed/additional/removed records, and one
  relocation with identical pixels. The same 233 inherited undecodable records
  remain outside pixel coverage.
- Correct emulator CRCs, zero occupied-video-byte changes during packaging,
  and 48 padding bytes changed for CRC correction.
- Unchanged pinned source inputs; no live installation.

The packaged ZIP SHA256 is
`6e6263cefa4952e81796fa5c8002d8a08a25683ae77ffbab067b24283859778b`.
The six-case runtime comparison at `tmp/battle-edge-runtime/` uses this exact
ZIP and its built checkout against the October 1 baseline:

| Controlled scenario | Matching RGB captures | Minimum free objects, baseline → candidate |
| --- | ---: | ---: |
| BATTLE, repeated Raiden lightning and uppercuts | **240 / 240** | 301 → 278 |
| BATTLE, Baraka decapitation | 43 / 43 | 317 → 294 |
| NUPOOL, repeated Raiden lightning and uppercuts | 240 / 240 | 228 → 214 |
| NUPOOL, Baraka decapitation | 43 / 43 | 269 → 253 |
| NUPOOL stage fatality, victim x=310, attacker on right | 42 / 42 | 237 → 224 |
| NUPOOL stage fatality, victim x=830, attacker on left | 45 / 45 | 246 → 232 |

All **653/653 full RGB captures match**, with no tolerance or cropped comparisons.
All cases have zero sampled DMA drops, peak overload 2 (matching baseline),
correct stage identity and intact free-object lists. Each combat case measures
7,203 fighting frames. The new runtime `summary.json` reports `passed: true`;
the earlier failed report is retained unchanged.

These controlled RAM setups cover selected matchups, effects and camera positions.
They do not establish equivalence for every frame, camera history or ordinary
match. Candidate object usage remains higher than baseline, as the minima show.
The live build and installed ROMs remain untouched, and the measured saving has
not been reallocated. Rejecting this experiment requires no live rollback.

## October 2: natural-play and object-drawing validation

The runner now includes four `play-*` scenarios: Liu Kang versus Jax and Baraka
versus Scorpion on BATTLE and NUPOOL. They use the game's
`battle_stability_sweep.lua` with matchup/stage setup before combat, a CPU
opponent, and natural health, timer and round outcomes. P1 walks left first,
reversing every 600 frames through ordinary input fields. No fighter positions,
health or winner state are pinned during fighting. This is scripted walking
against the CPU, not a human playthrough or comprehensive attack coverage.

Optional local sweep add-ons are disabled, inherited probe environment settings
are cleared, and the selected probe sources are hashed. Measurements require
at least 600 fighting samples within a 2,400-frame observation window, at least
100 pixels of camera travel, actual damage and the requested matchup. Capture
traces compare camera/health during fighting and state/timing through natural
round and result transitions. Snapshots continue after combat ends.

The first trials exposed coverage gaps rather than passing evidence: one natural
KO ended before an arbitrary 1,200-fighting-frame minimum; the original late
walking reversal also left the BATTLE/Jax camera within only 28 pixels. The final
tests retain a full observation window after a KO and use repeated, earlier
reversals. No pixel tolerance or transition exclusion was introduced.

The repeated BATTLE/Baraka mismatch also exposed a missing measurement.
`qdma_drops` counts the miscellaneous queue, not abandoned object drawing.
`MPROC.ASM` and `MKDISP.ASM:init_dma_regs` expose separate counters:

- `deep_dmaq`: maximum object queue depth, in words (six words per entry).
- `dmaq_over`: object queue overflows.
- `dmaq_late`: frames starting while the previous object queue still had work.
- `dmaq_lost`: maximum number of entries abandoned by a queue reset.

The harness now resets these write-only diagnostic counters at the first sampled
fight and reads them through subsequent transitions and emulator exit. It refuses
missing measurements, any candidate object overflow, queue depth above the
500-entry capacity, or increases in late frames or worst abandoned-entry count.
Inherited late drawing is recorded and compared, not described as zero drops.
CPU overload and miscellaneous drops are also monitored outside fighting.

The complete rerun is `tmp/runtime-pressure-20261002/`, using the same corrected
candidate ZIP recorded above. All input hashes still match after the run.

| Scenario | Matching RGB captures | Worst abandoned entries, baseline → candidate | Decision |
| --- | ---: | ---: | --- |
| BATTLE, walking Kang / CPU Jax | 80 / 80 | 104 → 100 | Pass |
| BATTLE, walking Baraka / CPU Scorpion | **81 / 82** | 111 → 108 | **Fail: round-introduction pixels** |
| NUPOOL, walking Kang / CPU Jax | 80 / 80 | **111 → 117** | **Fail: drawing pressure** |
| NUPOOL, walking Baraka / CPU Scorpion | 81 / 81 | **118 → 128** | **Fail: drawing pressure** |
| BATTLE, repeated lightning / uppercuts | 240 / 240 | 0 → 0 | Pass |
| BATTLE, decapitation | 43 / 43 | 136 → 132 | Pass |
| NUPOOL, repeated lightning / uppercuts | 240 / 240 | 0 → 0 | Pass |
| NUPOOL, decapitation | 43 / 43 | **125 → 138** | **Fail: drawing pressure** |
| NUPOOL stage fatality, attacker on right | 42 / 42 | 0 → 0 | Pass |
| NUPOOL stage fatality, attacker on left | 45 / 45 | 0 → 0 | Pass |

**975/976 sampled captures match, but only six of ten scenarios pass all checks.**
All have zero object-queue overflows and miscellaneous-queue drops. Every case
with nonzero abandoned entries has one late-drawing frame in each build. CPU
peak overload is 2 except the natural BATTLE/Jax case, where both builds reach 3.
No wrong fighting stages, changed matchups or broken free-object chains occur.
The natural BATTLE camera ranges from -2 to 442; NUPOOL ranges from 0 to 373.
All four input-driven cases observe P1's health reach zero without a health pin.

The visible failure repeats at `play-baraka-scorpion-2/*/screens/mk2/0036.png`,
frame 2516, `gs_round_intro` (7). Its 10,488 differing pixels include the upper
background and HUD. `tmp/natural-play-battle-repeat/` reproduces it, while
`tmp/natural-play-battle-baseline-control/` compares the baseline against itself
with 82/82 matching captures. This is not explained by baseline capture variation.

`tmp/natural-transition-trace/` adds diagnostic captures on every frame from
2500 through 2530. Both builds abandon queued work at frame 2514 during round
setup. The screenshots differ on frames **2515 and 2516**, then match from 2517
through the end of that trace window. Game state and health agree on every traced
frame. Queue depth at frame 2516 is 618 → 750 words. The trace narrows the problem
to round setup/drawing completion; it does not yet prove which subdivision or
draw operation should change. The diagnostic comparison has three differing
captures because frame 2516 is captured by both the normal and extra sampler.
Its wrapper is retained at `tmp/trace_natural_transition.py`, and its generated
Lua, logs and difference images are retained with the output.

Ten runtime-harness regression tests pass, including natural-play coverage,
negative camera coordinates, optional add-on exclusion, state divergence,
object-queue pressure increases, missing measurements and inherited late drawing.
The earlier six-case 653/653 pixel result remains accurate for its coverage;
its limited counters did not establish transition drawing equivalence.

The 10,054-byte saving is still measured, but the candidate remains on hold.
Next: isolate and reduce the placement/draw-cost contribution in a new scratch
candidate, then rerun this ten-case matrix. No live game source, installed ROM,
or source candidate was changed by these tests, and no savings were reallocated.

## October 2: reducing subdivision cost

The candidate helper now accepts `--max-added-objects=COUNT` (0–256, default 24)
and reports estimated bytes saved and added placements for each accepted image.
The limit applies to the whole stage, including when proposals are evaluated
one image at a time. Existing runtime-order, unaligned-input and shared-artwork
exclusions still apply. This is a limit on authored placements, not a guarantee
about visible objects or DMA completion at every camera position.

The original candidates add 24 placements to each stage. Searches from the
unchanged October 1 baseline give these smaller alternatives:

| Stage | Added-placement limit | Accepted image IDs | Estimated saving |
| --- | ---: | --- | ---: |
| BATTLE | 0 | 27 | 18 bytes |
| NUPOOL | 0 | 84, 221 | 26 bytes |
| BATTLE | 4 | 21, 27 | 674 bytes |
| NUPOOL | 4 | 21, 84, 30, 221 | 344 bytes |

The four-placement pair retains the original palettes and module bounds, with
63 BATTLE placements (originally 59) and 231 NUPOOL placements (originally 227).
Its first full build passes through Phase E and measures **1,024 bytes saved**,
compared with the 1,018-byte source estimate. BATTLE's four and NUPOOL's three
inherited authored/packed discrepancies retain their baseline decoded hashes.

The new packing layout changes one pixel in unchanged MK1CAVE artwork:
`fu7HDRS` header 26, pixel (0,0), baseline index 57 versus raw candidate 56.
The preservation pass restores that one physical bit plus the IRW checksum,
checks 570 sprite spans, and verifies the other static background tables.
The explicit selection is `--keep MK1CAVE:fu7HDRS:26`; the prior NUENT selection
is specific to the larger candidate and must not be reused blindly.

Sources and audit output are retained at `tmp/draw-cost-battle-four/` and
`tmp/draw-cost-nupool-four/`; the built raw copy is
`tmp/stage-savings-validation/draw-cost-four-candidate/`.
`tmp/draw-cost-four-inputs.json` pins the baseline inventory and replacement source
hashes, and `tmp/draw-cost-four-preservation/preservation.json` records the repair.
The prepared job is `tmp/draw-cost-four-ready-job/job.json`. The first preparation
attempt correctly refused stale copied video chips; after running makerom and
makevrom in the scratch copy, preparation passed. No reviewed source was changed
to bypass the check.

Individual four-placement searches also find larger estimated savings than the
current largest-image-first selection: BATTLE image 42 saves 1,280 bytes, image 6
saves 922, and image 9 saves 832; NUPOOL image 96 saves 616 and image 57 saves 176.
These are separate alternatives under `tmp/draw-cost-profile/`, not additive
savings or runtime-approved candidates. Ranking by saved bytes per added
placement warrants a separate comparison after establishing the runtime result
for the first reduced-cost pair.

Validation: candidate helper Release build, rejected negative/over-limit/malformed
CLI limits, and all three optimizer/runtime-order/validation CTests pass.
The independent reviewed rebuild completed at `tmp/draw-cost-four-reviewed-run/`:
479 unchanged background images, zero regressions, unchanged decoded sprite
identities, all twelve video chips checked, and zero occupied video bytes altered
by packaging. The ZIP SHA256 is
`daac55007bd83a8fd09470704f5297aab17f24c074edd0fa2cb50d7bfa79e531`.

The four previously failing scenarios were rerun at `tmp/draw-cost-four-runtime/`:

| Scenario | Matching RGB captures | Worst abandoned entries, baseline → candidate | Decision |
| --- | ---: | ---: | --- |
| BATTLE, walking Baraka / CPU Scorpion | 82 / 82 | 111 → 110 | Pass |
| NUPOOL, walking Kang / CPU Jax | 80 / 80 | 111 → 112 | Fail: drawing pressure |
| NUPOOL, walking Baraka / CPU Scorpion | 81 / 81 | 118 → 120 | Fail: drawing pressure |
| NUPOOL, decapitation | 43 / 43 | 125 → 125 | Pass |

All 286 sampled captures now match, but the combined four-placement candidate
still fails two pressure checks. BATTLE's sampled free-object minimum improves
from 288 in the 24-placement candidate to 306, versus 310 in the baseline.
The next isolated comparison retains this BATTLE source and uses NUPOOL's
zero-added-placement alternative. Passing one BATTLE scenario does not yet
establish that the new combination passes the full matrix. No candidate has been
installed or promoted.

The mixed candidate keeps BATTLE's four added placements and NUPOOL's original
placement count. Its first full build and independent reviewed rebuild both pass
through Phase E. The measured MK7 payload is **1,544,314 bytes**, saving **704
bytes** against the 1,545,018-byte baseline. Raw packing already matches all 479
unchanged background images: the compatibility check makes **zero bit changes**,
with identical input/output IRW hashes and an empty repair list.

Reproduce its source pairs from that frozen baseline, using separate new folders:

```text
studio_validation_candidate BASELINE/data/BATTLE.BDB NEW_BATTLE_FOLDER --runtime-order --max-added-objects=4
studio_validation_candidate BASELINE/data/NUPOOL.BDB NEW_NUPOOL_FOLDER --runtime-order --max-added-objects=0 --exclude-image=36 --exclude-image=39 --exclude-image=87 --exclude-image=90
```

The source copy is `tmp/stage-savings-validation/draw-cost-mixed-candidate/`;
`tmp/draw-cost-mixed-job/job.json` pins its independent rebuild at
`tmp/draw-cost-mixed-reviewed-run/`. The verified diagnostic ZIP SHA256 is
`31c65c4af68b2df3e7154d4ca82e0d3f7a9cdad19fe75e7d1a62322dc9601633`.
All twelve chips, unchanged background artwork and sprite identities pass;
packaging changes 48 padding bytes and no occupied video bytes. The earlier
four-placement-per-stage candidate and its failed runtime report remain intact.

`tmp/draw-cost-mixed-runtime/` passes all ten periodic-capture cases: **976/976
captures** and no increased pressure counters. However, the additional
frame-by-frame round-introduction check at `tmp/draw-cost-mixed-transition/`
still fails: **112/113 captures** match. Frame 2515 differs in 2,144 pixels in
the rectangle x=347..399, y=93..157; the following frame now matches. The smaller
candidate resolves the pressure increases and reduces the transient mismatch,
but does not eliminate it. It remains on hold despite the periodic matrix pass.

Natural-play cases now capture every frame for the first eight frames of each
game-state change, including round setup, round introduction, fighting and result
transitions. This uses observed state changes rather than a hardcoded failing
frame. The standard runner reproduces the mixed candidate's failure directly at
`tmp/draw-cost-mixed-dense/`: **120/121 captures** match. Reports now print explicit
PASS/FAIL and retain specific failure reasons even when every sampled screenshot
matches but a pressure counter increases. All ten harness regression tests pass.

The denser baseline-versus-itself control at
`tmp/draw-cost-dense-baseline-control/` passes **121/121 captures**. Thus the
new sampling reproduces the candidate difference without introducing baseline
capture drift in this case.

A zero-added-placement control was also checked: BATTLE image 27 and NUPOOL
images 84/221, retaining 59 and 227 placements respectively. Its first full build
measures **44 bytes saved**. This layout needs one physical bit restored at a
shared NUENT1/NUENT2 payload: both header index 3, pixel (3,2), raw index 0 versus
baseline index 2. The pass verifies all 570 parsed sprite spans and the unedited
background tables. Its explicit selections are `NUENT1:T1HDRS:3` and
`NUENT2:T2HDRS:3`. The source copy is
`tmp/stage-savings-validation/draw-cost-zero-candidate/`, the preservation receipt
is `tmp/draw-cost-zero-preservation/preservation.json`, and the pinned job is
`tmp/draw-cost-zero-job/job.json`. This is a diagnostic control, not a claim that
44 bytes is the best achievable saving.

The independent reviewed run at `tmp/draw-cost-zero-reviewed-run/` passes the
full build, all twelve video-chip checks, 479 unchanged background images, and
the strict sprite comparison. Its diagnostic ZIP SHA256 is
`b30965955c6c47dbab4c2a3cbb93a59b268a6490709dbe4a812e79b48e94a612`.
Pinned input inventories remain unchanged; the explicit one-bit preservation
correction reproduces exactly. No occupied video bytes change during packaging.

The strengthened ten-case run is `tmp/draw-cost-zero-runtime/`:

| Scenario | Matching RGB captures | Worst abandoned entries, baseline → candidate | Decision |
| --- | ---: | ---: | --- |
| BATTLE, walking Baraka / CPU Scorpion | 121 / 121 | 111 → 111 | Pass |
| NUPOOL, walking Kang / CPU Jax | **119 / 121** | 111 → 110 | **Fail: round-introduction pixels** |
| NUPOOL, walking Baraka / CPU Scorpion | 112 / 112 | 118 → 118 | Pass |
| NUPOOL, decapitation | 43 / 43 | 125 → 124 | Pass |
| BATTLE, walking Kang / CPU Jax | 129 / 129 | 104 → 104 | Pass |
| BATTLE, repeated lightning / uppercuts | 240 / 240 | 0 → 0 | Pass |
| BATTLE, decapitation | 43 / 43 | 136 → 136 | Pass |
| NUPOOL, repeated lightning / uppercuts | 240 / 240 | 0 → 0 | Pass |
| NUPOOL stage fatality, attacker on right | 42 / 42 | 0 → 0 | Pass |
| NUPOOL stage fatality, attacker on left | 45 / 45 | 0 → 0 | Pass |

**Nine of ten cases pass; 1,134/1,136 captures match.** Every case has the same
sampled free-object minimum as baseline, zero object/miscellaneous queue
overflows, and no increased pressure counter. The failure is visual, not a
larger count of abandoned entries: NUPOOL/Kang frames 2684 and 2685, both in
`gs_round_intro`, differ by 9,450 and 286 pixels respectively. The changed
rectangles are x=288..341, y=0..174 and x=339..348, y=0..61. Difference images
are retained beside that case's `comparison.json`.

The same dense NUPOOL/Kang scenario compared against the unchanged baseline
twice passes **121/121 captures** at `tmp/draw-cost-dense-nupool-control/`, so
baseline capture variation does not explain this result in the control run.

This rules out extra placements as a sufficient explanation for all observed
differences. It does not yet establish the exact drawing operation to repair.
The 44-byte control, 704-byte mixed candidate, 1,024-byte four-placement pair and
10,054-byte larger candidate all remain unpromoted. The next investigation is
round-reload drawing timing, using the dense capture gate; simply reducing the
placement cap is not enough. All source/ROM changes remain in separate scratch
copies, and no live rollback or reallocation has occurred.
