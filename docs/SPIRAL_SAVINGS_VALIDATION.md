# SPIRAL savings validation — October 3, 2026

**October 4 status: a smaller shared-palette diagnostic passes validation.**
It saves **3,286 packed video bytes**, adds no placements or palettes, and keeps
the linked program size unchanged. All **1,204 combat/navigation captures**
match. It requires a separate one-bit baseline-pixel preservation artifact;
the raw build is not accepted. Nothing is integrated or installed. See
[the follow-up](#october-4-isolate-palette-growth-and-reuse-the-existing-palette).
The earlier 5,348-byte candidate remains on hold after runtime failures.

The initial source audit below is under `tmp/spiral-audit-20261003/`; it did not
build the game. The subsequent builds use a fresh frozen snapshot under
`tmp/spiral-validation-20261003/`. NUPOOL optimization remains deferred; its
existing shared payloads are preserved here.

## Sharing changes the ranking

All 1,755 rows of the frozen baseline's 28 SPIRAL images decode to source
indices. Images **45 and 48** already share packed storage with TOMB (`HDRS`)
and NUPOOL (`OLHDRS`). The previous 504-byte, four-placement proposal subdivides
image 45, so its local saving cannot be counted as bank savings. The new audit
rejects that proposal before building; no actual bank growth figure is claimed.

Three alternatives preserve both shared images byte-for-byte:

| Alternative | Modeled video bytes saved | Added placements | Static checks for both consumers |
| --- | ---: | ---: | --- |
| Original palettes, four-placement limit | 268 | 4 | Pass; sky block counts need updating |
| Original palettes, 24-placement limit | 2,352 | 24 | Pass; custom references/counts need updating |
| Palette compaction, zero-placement limit | **5,342** | **0** | Pass with paired palettes and hut remap |

These alternatives are not additive. The palette proposal is the next build
candidate: it offers the largest estimate while retaining **65 placements** and
**28 images**. It changes images 3, 21, 33, 36, 39, 42, 51, 54, 63 and 81.
Nine compact palette variants increase the palette count from 7 to 16 and
modeled palette storage from 898 to 1,184 bytes. A corresponding Outer Haven
palette table is also required, so 286 bytes is not the full program-ROM cost.

## Consumer proof

Portal uses `spiral1BMOD` through `spiral4BMOD`, `alHDRS` and `alPALS`.
Outer Haven uses `ohspiral1BMOD` through `ohspiral4BMOD`, the same headers and
`ohPALS`. Three BLKS tables are shared directly; its pagodas instead use the
hand-authored `ohspiral2BLKS` table.

That table moves the left hut +201 pixels and the right hut −211 pixels from
their Portal positions. The right hut is mirrored. These offsets make the huts
visible at Outer Haven's camera extremes and must remain. The stock-record
promoter preserves source-only records: it does **not** automatically remap the
custom huts or update Outer Haven's module counts when SPIRAL changes.

The new read-only `tools/audit_spiral_consumers.py` checks:

- Source runtime headers agree with the generated packed snapshot, and baseline
  packed indices match the BDD.
- All eight known static module bindings, counts, header indices, palette slots
  and source bounds are valid.
- Opaque indices merge only if RGB555 agrees in both consumers. No conflicting
  Portal-only merges were found. Transparent zero stays distinct from opaque black.
- The palette candidate retains placement coordinates and Z values. Image
  canonicalization changes some X/Y flip flags; palette transfer and hut remapping
  explicitly account for both axes.
- Corresponding pixels yield a paired Outer Haven palette without color
  collisions. Retained original palette slots keep their unused colors too.
- Four layers × two consumers × two modeled X arrival orders give **16 exact
  RGB555-and-coverage comparisons**. All pass. Hut offsets, module origins,
  dimensions and counts remain unchanged for the palette candidate.

The report records proposed Outer Haven palette words and all 20 remapped hut
entries. This is a review recipe, **not an applied ASM patch or automatic export**.
Copying the candidate BDD/BDB alone would leave stale runtime palettes, header
references and flip flags.

This proof covers known static artwork. Palette fading/allocation, background
reloads, camera history and effects need runtime checks. Additional palettes can
cost runtime resources even without new placements. Portal's monks, floor and
warp animation and Outer Haven's custom stairs/monk movement are outside this
BDD-only proof.

## Reproduction and evidence

Use fresh output paths:

```text
studio_validation_candidate.exe FROZEN/data/SPIRAL.BDD NEW_PROPOSAL --runtime-order --max-added-objects=0 --exclude-image=45 --exclude-image=48 --compact-palettes
python -B tools/audit_spiral_consumers.py FROZEN NEW_REPORT.json --proposal NEW_PROPOSAL
```

Palette compaction is explicit and disabled by default. The shared-base search
rejects that flag instead of ignoring it. The consumer audit refuses unknown
geometry, palette collisions, missing records, changed shared images and
unsupported custom hut subdivisions.

Local evidence:

- `snapshot.json`, `source/`, `source-recheck.json`: capture and later drift check.
- `packed.json`: baseline identity and aliases.
- `proposal/`: rejected original 504-byte proposal.
- `protected-4/`, `protected-24/`, corresponding `*-final.json`: alternatives
  retaining original palettes and shared images.
- `palette-protected-0/`, `palette-consumers-reviewed.json`: preferred proposal,
  both-consumer proof, alternate palettes and hut remap.
- `SUMMARY.json`: decision, next steps and artifact/tool hashes.

All **5,594 frozen source files** still match their captured hashes. The live
checkout changed independently during this audit, including gameplay ASM and
build outputs; the recheck lists that drift. This experiment did not write
there. Results refer to the frozen snapshot; do not replace newer live work with
it. SPIRAL's BDD/BDB and reviewed background ASM were not among the changed files
at that check.

The native helper rebuilt successfully. Six new consumer tests, four packed-
verifier tests and three targeted native optimizer/order/validation tests pass.
The unsafe shared-image proposal and invalid shared/compaction flag combination
are rejected as intended.

## Full-build and runtime results

The October 3 follow-up uses 5,603 frozen source files and 654 retail reference
files. A live log changed while a second copy was being made; the hash check
stopped that copy. The already-completed `source/` snapshot verified in full.
Both build roots were then copied from that same snapshot, and the incomplete
copy was retained separately. No mixed live/frozen inputs were used.

Only four authored files differ in the candidate: `SPIRAL.BDD`, `SPIRAL.BDB`,
`BGNDPAL.ASM` (paired palette tables) and `BGNDTBL.ASM` (custom hut references).
Both builds complete LOAD2, assembly/link and checks through Phase E. The
candidate uses `ALLOW_STOCK_BG_DRIFT=1` for the deliberate BDD/BDB change;
other build guards remain enabled.

| Measure | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| MK7 payload bytes | 1,545,018 | 1,539,670 | −5,348 |
| Linked COFF bytes | 848,338 | 848,982 | +644 |
| Placements | 65 | 65 | 0 |
| Image headers | 28 | 28 | 0 |
| Palette slots per consumer | 7 | 16 | +9 |

Actual packing saves six bytes more than the model. Bank 1 space outside
declared reservations increases from 15,508 to 20,856 bytes; the largest
unreserved gap grows from 6,878 to 8,618 bytes. Slot bases/limits and physical
chip sizes remain unchanged. Program and video storage are separate budgets.

- All 1,755 SPIRAL rows match authored indices in each build. Both aliases to
  TOMB/NUPOOL remain at their original packed addresses.
- All **533 unedited static background images** match. The raw candidate
  passes; **no post-build pixel-preservation artifact is needed**.
- Linked COFF checks verify all eight module records, BLKS contents, header
  records, palette pointers and color words. All 16 static consumer/layer/order
  comparisons match after compilation.
- All twelve chip lanes and occupied ZIP video bytes pass verification. CRC
  padding is outside occupied payloads. Packaging uses `--no-install`.
- Sprite identity: 10,269 records, one relocation with identical pixels, zero
  changed/added/removed or stale-unreferenced records. The same 233 inherited
  undecodable records remain unverified.

| Runtime comparison | Matching captures | Result |
| --- | ---: | --- |
| Portal projectile | 240 / 240 | Pass |
| Portal decapitation | 41 / 43 | Fail: two frames |
| Portal Kang/Jax natural play | 129 / 129 | Pass |
| Portal Baraka/Scorpion natural play | 129 / 129 | Pass |
| Outer Haven projectile | 240 / 240 | Pass |
| Outer Haven decapitation | 43 / 43 | Fail: lost DMA entries 124 → 128 |
| Outer Haven Kang/Jax natural play | 129 / 129 | Pass |
| Outer Haven Baraka/Scorpion natural play | 128 / 129 | Fail: one frame; lost entries 122 → 124 |
| Portal camera pan | 35 / 35 | Pass |
| Outer Haven camera pan | 35 / 35 | Pass |
| Outer Haven complete staircase-view sweep | 52 / 52 | Pass |

Portal's changed fatality captures occur at frames 1494 and 1589 (samples 285
and 380). They contain 2,413 and 2,509 changed pixels respectively, within
x 53–347/y 81–168 and x 53–342/y 81–169. Outer Haven's changed natural-play
capture has 1,181 differing pixels at x 0–99/y 163–203. Full RGB images are
compared, without exempting transitions or screen regions. The drawing cause
has not been isolated.

Baseline-versus-itself controls for all three failing scenarios match
**215/215 captures**, with identical counters and state traces. All eight combat
comparisons measure zero palette-transfer drops and zero palette-allocation
failures. Those counters are observed from boot, including pre-fight setup.
Object minima, queue peaks, late-frame counts and miscellaneous DMA-drop counts
do not increase. The two increased lost-entry counts still fail validation.

The camera probes retain the original stage-specific structural checks and
record camera timing as well as pixels. A logger correction records Portal's
camera reach even though its whole-frame stairs are not multipart. The first
short staircase sweep did not meet the generic harness's capture-count minimum;
it was rerun with five-frame sampling. The final 52/52 result comes from
`navigation-sweep-dense/`; the earlier run is retained and excluded from totals.
This is bounded emulator coverage, not exhaustive gameplay or hardware proof.

Evidence in `tmp/spiral-validation-20261003/` includes `snapshot.json`,
`snapshot-copy-recovery.json`, `candidate-changes.json`, `consumer-proof.json`,
both build logs, `compiled-consumers.json`, `bank-comparison.json`, chip receipts,
`sprite-comparison.log`, `runtime/summary.json`, `runtime-control/summary.json`,
the navigation comparisons, `changed-captures.json` and final `SUMMARY.json`.
All 5,603 frozen inputs and both 654-file retail reference inventories recheck
unchanged. The live checkout continued changing independently; its drift is
recorded in `source-recheck.json`. No live rollback is needed for this experiment.

The runtime harness now supports Portal (7) and Outer Haven (22) and rejects
palette-allocation or transfer failures. Its 11 regression tests pass, including
the expanded stage routing and palette-counter failure cases.

## October 4: isolate palette growth and reuse the existing palette

Three follow-ups use the same frozen source and baseline. Every full build
passes through Phase E; all source and ROM changes remain in scratch copies.

| Experiment | Packed saving | Palettes per consumer | Linked COFF growth | First three runtime cases |
| --- | ---: | ---: | ---: | --- |
| Compact image 51 into a new palette | 1,988 B | 7 → 8 | +44 B | Hold: three changed captures; two DMA-loss increases |
| Duplicate the original palette for image 51; retain original pixels | 0 B | 7 → 8 | +268 B | Portal fatality still differs in two captures |
| Reindex the existing shared palette and all its users | **3,286 B** | **7 → 7** | **0 B** | **215/215 captures match; all three cases pass** |

The single-image version passes Outer Haven decapitation, but Portal decapitation
still differs in two captures (lost entries 126 → 129), and Outer Haven
Baraka/Scorpion differs in one (122 → 124). Its raw output also changes one
physical pixel shared by NUENT1/NUENT2. Runtime uses a separate, recorded
one-bit preservation copy; the raw build is retained.

The palette-only control has a byte-identical MK7 IRW and all 561 static images
match baseline. It reproduces precisely the two Portal differences, while both
Outer Haven cases pass. This shows that re-encoding video pixels is not required
to reproduce the Portal issue. It does not establish a unique engine cause:
adding a palette also changes program data/layout. The Outer Haven combat
difference is not reproduced by this control. Baseline captures and counters
repeat identically in both experiments (215/215 each).

### Shared-palette candidate

Instead of adding palette slots, reorder the existing 64 entries in palette 0
and reindex all nine images using it: **39, 42, 51, 54, 66, 69, 72, 75 and 81**.
The new-index-to-old-index permutation starts `0,1,3,4,5,45,62,63`, followed by
the remaining indices in ascending order. Transparency remains at zero. Apply
the same permutation to both Portal's `timeB_p` and Outer Haven's `oh_timeB_p`.
No colors are merged or discarded. The source reference search found no other
palette consumers outside these paired tables.

Only authored `SPIRAL.BDD` and `BGNDPAL.ASM` change. `SPIRAL.BDB` stays
byte-identical; all 65 placements, 28 image IDs/order, dimensions, offsets,
flip flags, hut references and seven palette slots remain. Images 45/48 retain
their cross-stage sharing. Actual packed headers lower image 51 from 6 to 3 BPP
and image 81 from 6 to 4 BPP. The other seven reindexed images keep their BPP.

MK7 decreases from **1,545,018 to 1,541,732 bytes**. Linked COFF remains
**848,338 bytes**. Free bank-1 space outside reservations increases from 15,508
to 18,794 bytes, with the largest unreserved gap unchanged at 6,878 bytes.
Physical ROM chip sizes and declared slot boundaries do not change.

Both consumers pass all 16 static layer/order comparisons and independent
compiled module, block, header and palette-word checks. All 1,755 packed SPIRAL
rows match the candidate's authored indices.

The raw build changes the bottom-right pixel of NUENT1 `T1HDRS:10` and NUENT2
`T2HDRS:12` from baseline 9 to 8. These aliases share one physical bit. The
explicit preservation artifact restores that bit at payload offset 11,964,336
and updates the IRW checksum; all 570 parsed sprite spans are checked for
overlap. It does not fix LOAD2 or change candidate-stage pixels. A separate
copy is verified to differ only in `MK7MIL.IRW` before packaging.

The preserved diagnostic passes all 533 unedited static background checks,
all twelve chip lanes and occupied ZIP-byte checks. Strict sprite comparison
finds 10,269 records, one identical-pixel relocation and zero changed, added,
removed or stale-unreferenced records. The same 233 inherited undecodable
records remain unverified. Packaging uses `--no-install`.

### Runtime results and decision

| Shared-palette diagnostic | Matching captures | Lost DMA entries, baseline → candidate | Result |
| --- | ---: | ---: | --- |
| Portal projectile | 240 / 240 | 0 → 0 | Pass |
| Portal decapitation | 43 / 43 | 126 → 126 | Pass |
| Portal Kang/Jax natural play | 129 / 129 | 97 → 97 | Pass |
| Portal Baraka/Scorpion natural play | 129 / 129 | 105 → 105 | Pass |
| Outer Haven projectile | 240 / 240 | 0 → 0 | Pass |
| Outer Haven decapitation | 43 / 43 | 124 → 124 | Pass |
| Outer Haven Kang/Jax natural play | 129 / 129 | 115 → 115 | Pass |
| Outer Haven Baraka/Scorpion natural play | 129 / 129 | 122 → 122 | Pass |
| Portal camera pan | 35 / 35 | No increase | Pass |
| Outer Haven camera pan | 35 / 35 | No increase | Pass |
| Outer Haven complete staircase-view sweep | 52 / 52 | No increase | Pass |

All eight combat cases have identical sampled metrics, including object minima,
queue peaks, late frames and lost entries. Palette failures/transfers dropped
and miscellaneous drops remain zero. Capture timing, gameplay-state traces and
navigation camera traces match; no screen regions are exempted. The eight new
baseline runs also match the October 3 baselines: **1,082/1,082 captures**, with
identical metrics. The navigation sweep uses five-frame sampling as before.

This is a passing isolated diagnostic under bounded emulator coverage, not an
exhaustive gameplay or hardware guarantee. Keep both added-palette candidates
on hold. The shared-palette result is the candidate to reproduce next; the
explicit preservation step remains required and is not a LOAD2 repair.

Evidence is under `tmp/spiral-validation-20261003/image51-20261004/`:
`runtime/` holds the single-image failure, `palette-control/` the unchanged-video
control, and `shared-palette/` the passing candidate. Each retains source-change
hashes, full-build logs, compiled checks, packaging receipts and runtime reports.
The shared candidate also retains raw/preserved bank comparisons and a receipt
showing only the IRW differed before packaging. `SUMMARY.json` pins the final
artifacts and scratch drivers. All 212 prior artifact hashes, 5,603 frozen source
files and 654 frozen reference files recheck unchanged. Independent live-checkout
drift is recorded in `source-final-recheck.json`; this work did not edit it.

## Next work

Turn the exact shared-palette permutation and preservation artifact into a
reproducible reviewed build job, then independently rebuild and repeat validation
from pinned inputs before integration. Preserve both stage palettes and every
user of their indices; do not expose a generic palette-reordering action until
dynamic consumers and cycling have equivalent reference checks. The library
audit UI and verified IMG animation export remain separate product tasks.
