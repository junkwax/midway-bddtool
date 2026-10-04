# MOUNTAIN savings validation — October 3, 2026

**Status: hold.** The smaller shared-base proposal saves **610 packed video
bytes**, but changes ladder and Yoteigai Tor captures and increases measured DMA
losses in combat probes. Both full builds and static reconstruction pass. Those
checks do not override runtime differences. Nothing was installed or applied to
the live game.

The isolated experiment is under `tmp/mountain-validation-20261003/`. Original
inputs, baseline build, raw candidate and explicitly preserved candidate remain
separate. NUPOOL optimization remains deferred.

## Consumers and candidate

MOUNTAIN is used by **both the one-player ladder and Yoteigai Tor** (`peak_mod`,
stage `0x18` / 24). Earlier planning described only the ladder. Peak reuses
`mount1BMOD` for quarter-rate mountain parallax and `mount2BMOD` for the fixed
sky; it also has storm effects and a separate stage fatality. A ladder-only or
combat-only check would miss part of this asset's use.

The baseline `tainHDRS` table has no duplicate-address aliases with other static
stage tables. This differs from the TOMB proposal that lost cross-stage storage
sharing. It does not mean MOUNTAIN has only one runtime consumer.

The native candidate generator's new `--shared` option selects the existing
shared-base/detail search. With a four-placement ceiling and original palettes,
it changes images **27, 54, 153 and 183** into two families of shared bases and
unique detail pieces. Only `data/MOUNTAIN.BDD` and `data/MOUNTAIN.BDB` are authored
changes. Both module bounds remain unchanged; no BGND placement correction is
needed. Independent saved-file comparisons pass both modeled X arrival orders
with the flip axes respected, and all five palettes are identical.

| Measure | Baseline | Candidate | Change |
| --- | ---: | ---: | ---: |
| MK7 actual payload bytes | 1,545,018 | 1,544,408 | −610 |
| MOUNTAIN model bytes | 151,604 | 150,994 | −610 |
| Image headers | 63 | 65 | +2 |
| Stored placements | 106 | 110 | +4 |
| Palette bytes | 574 | 574 | 0 |
| Linked COFF bytes | 848,338 | 848,390 | +52 |

The receipt reports 610 additional bytes outside declared reservations in bank
1, increasing that total from 15,508 to 16,118 bytes. The largest unreserved gap
remains 6,878 bytes. No slot map or neighboring asset moves, and physical ROM
chips remain their original size. Video payload and program-table bytes are
separate budgets.

## Build and packed-art checks

Baseline and raw candidate complete LOAD2, assembly/link and checks through
Phase E. The candidate uses `ALLOW_STOCK_BG_DRIFT=1` for this deliberate source
edit; other guards remain enabled. All 3,398 baseline and 3,576 candidate
MOUNTAIN decoded rows match their authored indices.

Raw packing changes one shared pixel in `NUENT1:T1HDRS:3` and `NUENT2:T2HDRS:3`:
pixel (3,2) becomes index 0 instead of baseline index 2. This is the previously
identified LOAD2 bit-overflow/alignment behavior. Raw output therefore fails the
unedited-art check.

An **explicit, separate preservation artifact** restores that baseline pixel
with one physical payload-bit change plus the IRW checksum. Its receipt checks
570 possible sprite spans and preserves all edited-stage pixels. The saving
remains 610 bytes. Runtime tests use this preserved artifact; the raw candidate
remains available for inspection. This is neither a packer fix nor an automatic
UI/export step.

- All **498 unedited static background images** match after preservation.
- Native receipts verify all twelve video chip lanes and the slot map.
- The sprite comparison has 10,269 records, one relocation with identical pixels,
  zero changed/added/removed records and zero stale-unreferenced records. The
  same 233 inherited undecodable records remain unverified.
- Diagnostic ZIP packaging uses CRC padding outside verified occupied payloads.
  Every occupied video byte matches the checked raw chips. No install runs.

## Runtime results

Full RGB images are compared; no screen region or transition frame is exempted.

| Probe | Matching captures | Result |
| --- | ---: | --- |
| Peak Raiden projectile test | 240 / 240 | Pass |
| Peak Baraka decapitation | 43 / 43 | Fail: DMA lost entries increase 102 → 104 |
| Peak Liu Kang versus Jax | 127 / 128 | Fail: one capture; lost entries 69 → 70 |
| Peak Baraka versus Scorpion | 127 / 129 | Fail: two captures; lost entries 74 → 77 |
| Full ladder, Sub-Zero selection | 523 / 531 | Fail: eight captures |
| Full ladder, Rain selection | 521 / 532 | Fail: eleven captures |
| Full ladder, Ermac selection | 521 / 532 | Fail: eleven captures |

A separate Peak camera-stop/animation probe also fails its timing check: both
runs capture stage selection at the same frame, but all candidate fighting
captures arrive one frame later. The baseline-only control matches all 69
captures and their timing. This run is not counted as a passing camera check;
its diagnostic comparisons remain under `navigation-runtime/`.

The ladder probe confirms a fighter, releases the confirm button and records
every frame until fighting begins. This permits the full pan and fade instead
of skipping the ladder. World Y spans −48 through 784. Candidate and baseline
capture timing, game-state and world-Y traces match. Differences occur in state
7, at relative frames 375–385 and 434 for Sub-Zero, and 366–386 and 435 for Rain
and Ermac. `changed-captures.json` records every affected frame, pixel count and
bounding box; the precise rendering cause is not isolated.

Ladder queue, late-frame and lost-entry peaks are unchanged, but the candidate
uses three more objects at the measured free-object minimum (185 → 182).
Peak controlled combat uses two more objects; queue-word peaks increase by up
to 12. Neither candidate has a new queue overflow or miscellaneous DMA drop.
The increased combat lost-entry counts and changed pixels still fail validation.

Baseline-versus-itself controls for the three failing Peak scenarios match
**300/300** captures with identical counters and traces. Natural-play tests
include damage, camera travel from X 20 to 400 and two exits from fighting.
The three baseline-only ladder controls match **1,595/1,595** captures with
identical counters and traces.
These are bounded emulator checks, not exhaustive gameplay or hardware proof.
The separate Peak stage fatality has not been validated for this candidate.

## Evidence, rollback and next work

Local evidence includes:

- `snapshot.json`, `source/` and `source-recheck.json`: frozen inputs and later
  live/reference drift checks.
- `proposal/`, `candidate-changes.json`, `source-layer-check.json`: candidate
  construction, unchanged module bounds and palette/layer proof.
- `baseline-build.log`, `candidate-build.log`, `baseline-packed.json`,
  `candidate-packed.json`, `bank-comparison.json`: full-build and raw packed data.
- `preservation/preservation.json`, `preserved-bank.json`, native `.romreceipt`
  files and `sprite-comparison.log`: compatibility artifact and bank verification.
- `peak-runtime/`, `peak-control/`, `ladder-runtime/`, `ladder-control/`:
  probes, process logs, full captures and comparison receipts.
- `navigation-runtime/`, `navigation-control/`: additional timing failure and
  its baseline-only control.
- `SUMMARY.json`: final decision and hashes of the review artifacts.

No live rollback is needed. Original files and all candidate variants remain
separate; do not overwrite a newer live checkout with the frozen source copy.
Final inventories match all **5,594 live inputs**, all **654 reference inputs**
and both frozen copies, with no changed, missing or added inventoried files.

The generator rebuild and three targeted native tests (optimizer, runtime order
and validation checks) pass. The runtime harness now includes stage 24; the
packed verifier recognizes `tainHDRS`. All 11 runtime-harness and four packed-
verifier Python tests pass. Invalid combinations of `--shared` with
`--runtime-order` or image exclusions are rejected before creating output.

Keep this candidate on hold. **SPIRAL is next for a reference and alternate-
palette audit** of both Portal and Outer Haven before any export. Its source
savings cannot be treated as bank savings until existing aliases and the
hand-authored `ohspiral2BLKS` / `ohPALS` consumers are accounted for.
