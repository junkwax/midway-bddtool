# Next stage savings: October 2, 2026

**October 3 update:** [TOMB's isolated validation](TOMB_SAVINGS_VALIDATION.md)
found that the 566-byte local proposal grew the bank by 4,512 bytes after losing
cross-stage sharing. A revised proposal preserves those aliases and saves 174
packed bytes, but three round-redraw captures differ; integration remains on
hold. [MOUNTAIN testing](MOUNTAIN_SAVINGS_VALIDATION.md) subsequently confirmed
610 packed bytes saved, but found ladder/Peak rendering differences and higher
combat DMA losses; it also remains on hold. [SPIRAL validation](SPIRAL_SAVINGS_VALIDATION.md)
now measures 5,348 packed bytes saved with no added placements or unedited-art
changes. Three runtime captures differ and Outer Haven DMA losses increase;
integration remains on hold. Next, isolate one palette-compacted image and repeat
the failing probes. The table below remains
the October 2 per-file model, not bank savings.

**Build follow-up:** the fresh MK3CAVE experiment reproduced 3,096 packed bytes
saved, but broader runtime tests found changed frames. Both full builds also
stopped at an unrelated source-palette guard. See [the cave findings](MK3CAVE_VALIDATION.md#october-2-fresh-build-and-combat-validation).
TOMB was tested next as recorded above. Cave integration remains on hold.

NUPOOL savings work is deferred at the user's request. Its bodies that fall into
the acid and change between rounds are background animations. The existing
BATTLE/NUPOOL experiments remain on hold; this pass does not revisit them or
attribute their rendering differences to a particular animation.

Fresh copies of **MK3CAVE, MOUNTAIN, SPIRAL and TOMB** were audited with zero,
four and 24 additional placements allowed. Each limit runs four independent
alternatives: subdivision/reuse and shared-base/detail, with and without palette
compaction. All 12 runs completed: 40 of 48 alternatives passed exact image
reconstruction and static runtime Z/Y ordering in both X arrival directions.
Eight MOUNTAIN alternatives failed ordering verification. A passing no-op is
included in that count; passing does not imply a positive saving.

All eight frozen BDD/BDB files and **1,882 inventoried live source/build/ROM
file hashes** remained unchanged. No candidate was applied, no game build or
packer ran, and no ROMs were installed.

## Fresh estimates with original palettes

These are modeled video bytes, not measured LOAD2 savings. Each cell is the best
verified alternative at that placement limit. Columns are alternatives and must
not be added together. A larger search can select a proposal that fails; it is
not evidence that a smaller passing proposal stopped working.

| Source | No added placements | Up to 4 added | Up to 24 added | Actual added placements / table growth at largest passing limit |
| --- | ---: | ---: | ---: | --- |
| MK3CAVE | 56 B | 772 B | 3,078 B | +24 / +432 B |
| TOMB | 62 B | 566 B | 1,200 B | +12 / +186 B |
| SPIRAL | 0 B | 504 B | 2,706 B | +23 / +394 B |
| MOUNTAIN | 0 B | 610 B | Unverified | +4 / +52 B (shared-base, four-placement limit) |

Table growth is separate program-ROM usage. Placement counts describe the BDB,
not peak runtime object use or DMA cost. Zero additional placements can still
change image bounds, packing and overlap behavior.

## Work order and stage-specific checks

1. **MK3CAVE: isolated validation now on hold (see follow-up above).** The fresh 3,078-byte estimate
   reproduces the model behind the earlier **3,096-byte measured** result; the
   measured result was subsequently reproduced in the isolated follow-up. Keep the seven palettes,
   original source coordinates and protected `mk3cave3` cavern/water layer.
   The four-placement alternative saves an estimated 772 bytes with 72 bytes of
   table growth. It splits image 99 into five pieces and retains single-piece
   trims, including images 45 and 48. Compare it with the existing 24-placement
   candidate if draw pressure is a concern. Finish a fresh protected export/full
   build, decoded bank-wide checks, dense round-transition captures and active
   combat/camera tests before integration. The earlier saving mostly remains
   inside the MK3CV4 reserved slot; making it available to other assets requires
   a separate allocation change. See [cave validation](MK3CAVE_VALIDATION.md).
2. **TOMB: tested, integration on hold (see October 3 update).** The four-placement alternative
   splits image 42 into three pieces at two uses. Image 0, used 37 times, and
   image 66 also receive transparent-edge trims. Check generated layer bounds
   and centering, both camera extremes, both facing directions and round reloads.
   `calla_tomb` also spawns bats and configures a skewed floor; those runtime
   effects need capture coverage even though their animation assets are outside
   this BDD scan. Start with the 566-byte/four-placement proposal before the
   1,200-byte/12-placement alternative.
3. **SPIRAL: full candidate built and tested; integration on hold.** Portal and Outer Haven share
   `alHDRS`. Outer Haven uses `ohPALS` and a separate `ohspiral2BLKS` placement
   table with moved, mirrored huts. Source-BDB pixel proof does not cover those
   alternate palettes or hand-authored references. Preserve the hut offsets,
   remap every affected header reference and verify both stages. The palette
   alternative offers **5,342 modeled bytes with no added placements**, but
   increases palettes from 7 to 16 and palette storage by 286 bytes. This is a
   candidate with paired Outer Haven palettes and a hut remap. The full-build
   result saves 5,348 bytes, but runtime checks fail; next is a single-image
   palette experiment, beginning with image 51. The original 504-byte split
   changes image 45, already shared with TOMB/NUPOOL. Protecting images 45/48
   yields alternatives of 268 bytes at four added placements and 2,352 at 24.
4. **MOUNTAIN: smaller shared-base lead tested; integration on hold.** Both subdivision
   modes fail draw-order checks at every tested limit; both shared modes also
   fail at 24. The 610-byte proposal shares bases across images 27, 54, 153 and
   183, adding four placements while keeping five palettes. The earlier
   18,042-byte palette estimate is not a current verified candidate. This is the
   one-player ladder background (`mountain_mod`) and is also reused by Yoteigai
   Tor (`peak_mod`, stage 24). Both consumers were tested. Static reconstruction
   and full builds pass, but 30 ladder captures and three Peak combat captures
   differ; combat lost-entry counters also increase. See the linked validation
   report for the separate preservation artifact and repeatability controls.

Palette-changing cave alternatives also pass the source checks (4,212 bytes at
zero added placements), but the protected cave exporter requires the original
seven palettes. They are not part of the next candidate. Deliberately replacing
unique spikes or rocks with similar repeating/mirrored artwork is an art change
and remains separate from these lossless estimates.

## Evidence and reproduction

- [Complete CSV](../tmp/next-stage-audit-20261002/summary.csv): all 48 alternatives,
  verification status, bytes, table/palette growth and placement counts.
- [Audit receipt](../tmp/next-stage-audit-20261002/audit.json): commands, analyzer
  hash, results and unchanged-source check.
- [Frozen-input hashes](../tmp/next-stage-audit-20261002/snapshot.json),
  [live hashes before](../tmp/next-stage-audit-20261002/source-before.json) and
  [after](../tmp/next-stage-audit-20261002/source-after.json).
- [Local rerun driver](../tmp/audit_next_stages.py); choose a new output directory
  before rerunning. Private copies and detailed reports remain ignored in `tmp/`.

Build `studio_savings_audit` and analyze a frozen source pair with a new report
directory for each limit. The new optional flag accepts 0 through 256; omitted,
it retains the previous default of 24:

```text
studio_savings_audit.exe FROZEN/MK3CAVE.BDD NEW_REPORT --max-added-objects=4
```

The tool only analyzes; it never applies a proposal or saves a document. Its
existing cave-water protection remains enabled. Rebuilt successfully; the 12
audit runs respected their placement limits, the legacy CLI matched explicit
24-placement output, and five invalid/unknown-argument checks failed before
creating report directories.
