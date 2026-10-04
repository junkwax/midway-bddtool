# TOMB savings validation — October 3, 2026

**Status: hold.** A candidate that preserves cross-stage shared images saves
**174 packed video bytes**, adds three placements and passes both full builds,
packed-art checks, camera navigation, projectile and decapitation probes.
Three dense round-redraw captures still differ. Nothing has been installed.

The experiment uses frozen copies under `tmp/tomb-validation-20261002/`; the
directory retains its original start-date suffix. The baseline, rejected first
candidate and revised candidate are separate directories. No other stage's
authored artwork was optimized.

## Why the first candidate was rejected

The four-placement source proposal estimated a 566-byte saving. Actual LOAD2
packing instead grew MK7 from **1,545,018 to 1,549,530 bytes**, a **4,512-byte
increase**. The build stopped because the enlarged bank overlapped NOOBRING's
active payload. No overlap guard was disabled and this candidate was not
packaged or emulated.

TOMB's image 42 already shares packed storage with NUPOOL (`OLHDRS`) and SPIRAL
(`alHDRS`). Splitting the TOMB copy cannot remove the original payload while
those other consumers retain it. Images 6, 9 and 39 also share bank storage.
This is why per-file optimizer savings cannot be summed into a bank budget.

The revised search excludes images **6, 9, 39 and 42**. It splits image 57 into
four pieces and keeps the single-piece changes to images 63, 0 and 66. The
four-placement ceiling remains in force, but only three placements are added.
All eight palettes are preserved.

## Packing and generated placement checks

| Measure | Baseline | Revised | Change |
| --- | ---: | ---: | ---: |
| MK7 actual payload bytes | 1,545,018 | 1,544,844 | −174 |
| TOMB model bytes | 43,230 | 43,056 | −174 |
| TOMB image headers | 23 | 26 | +3 |
| Stored placements | 220 | 223 | +3 |
| Palettes | 8 | 8 | 0 |
| Linked COFF bytes | 848,338 | 848,392 | +54 |

Video bytes and program-table bytes occupy different ROM spaces. The fixed
video chips do not shrink. The bank receipt reports 174 additional bytes outside
declared reservations, with the largest unreserved gap unchanged at 6,878 bytes;
no slot map or neighboring asset was moved.

Trimming image 0 removes one transparent row at the fence module's origin:
`TMOD2` starts at source Y 228 instead of 227. The candidate changes the fence
placement in `BGND.ASM` from Y 108 (`06ch`) to 109 (`06dh`). Independent layer
comparison proves that adjustment is necessary and that all three layers then
match in world coordinates under both modeled X arrival orders, including both
flip axes. Layer widths and centering remain unchanged. The existing unassigned
placement is retained; it is not treated as proven unused artwork.

Baseline and revised candidate both complete LOAD2, assembly/link and checks
through Phase E. The candidate uses the build's explicit
`ALLOW_STOCK_BG_DRIFT=1` setting for this deliberate TOMB edit; all other guards
remain enabled. Source and packed checks establish the unaffected scope.

- All 1,031 baseline and 1,027 candidate TOMB rows decode to their authored
  pixels; all four cross-stage aliases survive.
- All **538 unedited static background images** match in the packed bank.
- Native receipts verify all twelve video chip lanes and the current slot map.
- The sprite audit reports 10,269 records, zero changed/added/removed records,
  zero stale-unreferenced records and one relocation with identical pixels.
  The same 233 inherited undecodable records remain unverified.
- Packaging uses only CRC padding outside verified payload extents. Every
  occupied video byte in the ZIPs matches the checked raw chips.

## Runtime results

| Probe | Matching captures | Result |
| --- | ---: | --- |
| Stage selection, camera stops, floor and bat-animation samples | 69 / 69 | Pass |
| Raiden projectile test | 240 / 240 | Pass |
| Baraka decapitation | 43 / 43 | Pass |
| Liu Kang versus Jax, natural play/transitions | 131 / 132 | Fail: one frame |
| Baraka versus Scorpion, natural play/transitions | 131 / 133 | Fail: two frames |

The failures occur while the background is being redrawn between rounds. The
changed captures are at frame 2546 for Kang/Jax and frames 2545–2546 for
Baraka/Scorpion, all in game state 7 before fighting resumes. The
Kang/Jax difference is at the right edge, x 364–399 and y 0–206 (1,913 changed
pixels). The Baraka/Scorpion differences cover x 162–195, y 149–206 (1,751 pixels)
and x 330–399, y 0–206 (4,209 pixels). Images are compared in full; no regions or
transition frames are exempted. The precise drawing cause is not isolated.

Baseline-versus-itself controls for both failing scenarios match **265/265**
captures, with identical pressure and play-state traces. Both natural-play
probes traverse camera X 431–831, observe natural damage and two exits from
fighting, and include 41 dense transition captures. This is bounded emulator
coverage, not exhaustive gameplay or physical-hardware validation.

Queue-word peaks increase by up to 18 while staying below capacity. No new
overflow, miscellaneous DMA drop, peak-overload increase, late frame or lost
entry is measured. The controlled probes use three more objects at their
measured minima; the natural-play minima are unchanged. These counter results
do not override the three changed frames.

An initial navigation probe went left around the long stage cycle and missed
TOMB before selection timed out. Its incomplete output remains under
`navigation-runtime/` and is excluded from the results. The corrected probe
selects TOMB by pressing right and records the 69 matching captures under
`navigation-right-runtime/`.

## Evidence and rollback

Local evidence under `tmp/tomb-validation-20261002/`:

- `snapshot.json` and `source/`: frozen original inputs; `source-recheck.json`
  records subsequent source/reference drift separately from candidate changes.
- `proposal/`, `candidate/`, `initial-bank.json`: rejected growth experiment.
- `shared-safe-proposal/`, `shared-safe/`, `shared-safe-changes.json`: revised
  BDD/BDB, the one-pixel fence correction and changed-file hashes.
- `runtime-layer-comparison.json`, `shared-safe-packed-tomb.json`,
  `shared-safe-bank.json`, `.romreceipt` files and `sprite-comparison.log`:
  layer, packed-row, bank, chip and sprite evidence.
- `runtime/summary.json`, `runtime-control/summary.json` and
  `navigation-right-runtime/comparison.json`: comparisons, logs and PNGs.
- `SUMMARY.json`: final hold decision, source rechecks and artifact hashes.

The complete original BDD/BDB and BGND source remain in `source/`. No rollback
of the live game is required because this experiment never applied there.
Final hashes match for all **5,594 live inputs**, all **654 reference inputs**
and the complete frozen source copy; no added files were found in those inventories.
Do not copy that older snapshot over a live checkout that has since changed.
Any later integration needs a fresh build and another runtime review.

The native candidate generator completed successfully. All 11 runtime-harness
tests and four packed-verifier tests pass. The harness now supports TOMB's
stage ID 1; the packed verifier recognizes its `HDRS` table.

## Next work

Defer TOMB integration. The subsequent [MOUNTAIN validation](MOUNTAIN_SAVINGS_VALIDATION.md)
measured 610 packed bytes saved, but ladder/Peak runtime checks failed; it remains
on hold. SPIRAL is next for Portal/Outer Haven palette and placement reference
review. Cross-stage sharing belongs in future audit
ranking so proposals that increase the actual bank are rejected earlier.
