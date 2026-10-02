# BATTLE and NUPOOL isolated savings validation

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

1. **Reproducible editor build workflow.** Integrate baseline capture, reviewed
   preservation where needed, final chip/artwork checks and no-install packaging
   into one recorded job. Refuse stale inputs and unreviewed corrections. Keep a
   deterministic LOAD2 repair and authored-art corrections as separate projects.
2. **Broader runtime validation.** Exercise longer combat, effects-heavy encounters
   and fatalities before promoting candidates or reallocating their saved space.
3. **Library-wide savings view.** Bring the existing read-only batch audit into the
   editor, distinguish source masters from generated packs, and rerun estimates
   through the stricter runtime-order verifier. The September estimates are historical.
4. **Verified animation export.** Extend IMG/multipart optimization through packing
   and runtime validation, including Forest face animation and palette behavior.
5. **Reference-proven unused-art removal.** Expand the existing audit into reviewed
   removal with dynamic/code references accounted for and an undo/rollback path.

No candidate has been promoted or installed. LOAD2 buffer-context and bit-overflow
behavior remain separate from the static runtime-order model.
