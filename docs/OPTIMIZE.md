# Stage optimization and pattern editing

Open a paired BDB/BDD stage and select **Optimize → Find savings**. The scan
runs in the background and can be cancelled. It analyzes a snapshot; edits
made during or after analysis make that proposal stale. Nothing is written to
the game checkout by analysis or Apply.

The first implementation targets static background artwork. It searches:

- Compact palette copies for the indices actually used in each region.
- Horizontal and vertical cuts with background widths aligned to four pixels.
- Repeated subdivisions, including mirrored halves, thirds, fifths and strips.
- Constant-size repeat groups with partial edge pieces.
- Exact shared tile payloads across processed images, with X, Y and XY flips.
- Empty-margin cropping and subdivision along internal transparent corridors.

Quick search samples cut positions. Deep search considers every aligned X cut
and every Y cut in its bounded greedy partition search. Both consider regular
subdivisions, constant-size strips and blank-space subdivisions, and compare
index-preserving alternatives. Empty-space cuts can progress through temporarily
unprofitable splits to expose several separate islands. This is not an
exhaustive search of every possible rectangle or partition combination.

## Review and apply

Select a source image to compare its original against the reconstruction.
Colored cut outlines label the shared tile ID, BPP, and X/Y flips; hover a
piece for details. Cycle through the palettes used by its placements.
Disable the cut overlay to inspect the reconstructed image alone.

The combined proposal shows video-data estimates separately from table bytes,
palette data, image count and placement count. Per-image work is accumulated
into one proposal, so sharing is included once in the final total. Some
one-piece replacements canonicalize an image to enable reuse by later images;
they do not necessarily save bytes in isolation.

Choose **Smallest video data**, **Balanced**, or **Fewer placements**. The
latter two penalize extra placements in the search score. Search limits bound
pieces per source image, additional placements and total palettes. The default
palette cap is 45 from the reviewed checkout's background-palette budget, not
a universal hardware limit or a measurement of this stage's runtime peak.
Existing palettes are retained; a stage already over the cap can still receive
index-preserving changes, but cannot add palettes until the cap allows it.

Palette copies preserve every source palette used by the current placements,
including opaque black and transparent index zero. Existing palettes are not
modified. Static-color equality does **not** prove compatibility with runtime
palette cycling, swaps or direct index references. Applying a proposal with
palette remapping therefore requires marking those affected palettes as static.
For index-preserving proposals, turn off **Try compact palette copies** and
scan again. Original palette slot assignments are preserved in that mode.

**Apply verified proposal** is one undoable document command. It rewrites all
uses of affected images, adjusts split-piece positions for existing X/Y flips,
shares payloads through explicit placement flips, and removes the replaced
source payloads. Save and game integration then use the normal workflows.
Normal metadata is retained; new image IDs are allocated without reusing old
ones. Images carrying animation anchors, LOD references or other runtime
metadata, locked placements, unassigned artwork, and incompatible source
geometry are excluded or refused rather than silently changed.

## Repeat & Mirror: deliberate artwork changes

Select **Optimize → Repeat & Mirror** to try replacing unique variations with
a reusable group or a mirrored side. This is separate from the lossless scan:
it can change both shading and silhouette. Nothing changes until you click
**Apply artwork change + reuse**.

- **One image (all uses)** lets you use the artwork selected on the stage or
  choose an image. Every placement of that image is affected.
- **Whole layer** composes the layer in draw order, respecting existing X/Y
  flips and empty gaps. Choose a group within that composition and repeat it
  across the full artwork bounds. This handles spike strips built from many
  distinct source images. Images still used by other layers are retained.
- **Repeat across X/Y** selects a source offset and group width/height. You
  can alternate mirrored groups. The final group is clipped to the existing
  extent. Horizontal group boundaries align to four pixels.
- **Find closest group** samples source windows at the chosen group size and
  suggests the one with the lowest changed-pixel score, adding extra weight to
  silhouette changes. It samples up to roughly 130 windows; it does not prove
  visual quality or choose an art direction for you.
- **Mirror left/right** or **Mirror top/bottom** keeps the chosen side and
  reflects it across the center. Odd center rows are retained.

The preview outlines the source group in blue, shows original/proposed artwork,
and optionally highlights changed pixels. Zoom and scroll to inspect long
strips. Pixel counts include any difference across the currently used palette
variants; silhouette counts measure transparent/opaque changes. Palette cycling
lets you inspect each variant. No palette indices are remapped by this tool.

Preview also runs a focused lossless packing pass on the *proposed* artwork.
The displayed estimate includes those splits and shared tiles, plus the resulting
placement and table counts. Apply performs the artwork change and packing
together as one undoable edit. It rejects stale previews and rechecks the source
group, pixel reconstruction and placements before applying.

Whole-layer patterns currently require visible, unlocked objects sharing one
palette slot and draw mode. Animation/LOD metadata is excluded. The composed
extent is bounded to 4096 pixels per axis and one million pixels, and must pack
within 16 pieces. Increase the group size if it cannot fit those limits. This
is an authoring choice, not an automatic search for visually acceptable edits.

On the local MK3CAVE input, repeating a 128-pixel group starting 128 pixels into
the final layer replaced 26 spike placements with 12. Stage totals changed from
64 to 50 placements and the video estimate from 203,238 to 189,504 bytes
(13,734 fewer). It changed 14,347 pixels (about 26% of the composed strip),
including 8,946 silhouette pixels. This is an example to review, not a selected
art direction or a verified packed-ROM saving. Tests wrote only scratch output.

## What is proved for lossless proposals

Before enabling Apply, the verifier reconstructs every changed source image
for every palette used by its placements. It compares raw RGB555 colors and
transparency at every pixel, checks disjoint pieces and bounds, and verifies
placement coordinates, flags, palette references, order and layer projection.
Apply repeats verification and rejects stale snapshots.

Tests additionally render complete scenes before/after, cover all four object
flip combinations and two palette variants, and compare after undo, redo and
save/reopen. Negative controls change a mirror or a placement and must fail.
Whole-image XY sharing, preserved palette slots, placement limits and
cancellation are covered independently.

## What the byte figures mean

Video-data estimates model background tiles using automatic BPP, LOAD2-style
row compression and 16-bit alignment. Raw bit-packed totals are also kept in
the budget structure. Exact identical payloads of matching dimensions count
once; no checksum-only equality is assumed. Table and palette figures are
separate approximate storage costs, not deductions from video-ROM savings.

The selected LOD can force different BPP, compression or alignment. LOAD2
checksum folds, bit bleed and bank allocation require checking actual packed
output, per label and orientation. This implementation does not run LOAD2,
measure free bank extents or claim reclaimed ROM bytes. It also does not
measure runtime peak objects, palette slots or DMA usage. A larger number of
pieces can cost more runtime work even when the video payload shrinks.

The local MK3CAVE deep/balanced benchmark modeled 203,238 → 192,058
video bytes (11,180 bytes, about 5.5%), with 64 → 128 placements and 7 → 45 stored
palettes. Those are estimates for that local input snapshot, not shipped
asset data or a packed-ROM measurement. The scratch output passed complete
scene equality and save/reopen tests; the live source pair was not modified.

## Research reviewed

The implementation was informed by these notes in the user's `mk2-main`
checkout, under `doc/graphics_opportunities/`:

- `BUDGET_AUDIT.md`: distinguish object, palette and DMA pressure from ROM size.
- `PALETTE_REINDEX_SCAN.md`: individual palette savings can be invalid when
  palettes are shared or index-sensitive.
- `PARTITION_SCAN.md`: subdivision trades stored art against per-piece costs;
  merging and splitting must be evaluated rather than presumed beneficial.
- `SUBFRAME_SHARE_SCAN.md`: repeated subregions can save space, but sparse
  shared pixels may barely shorten the encoded row spans.
- `COMP_POLICY.md` and local LOAD2 background/compression source: compression
  policy and row-encoding behavior matter; raw-pixel savings are not receipts.
- `LOAD2_MIRROR_FOLDS.md` and `KAHN_FIST_REUSE_PROTOTYPE.md`: checksum aliases
  and different draw consumers require explicit flips and packed-pixel proofs.

Private game assets and source-drop code are not included here. Future passes
can add arbitrary-offset shared subregions, exact residual patches, global
palette-family planning and build-backed per-bank receipts. Runtime actors and
foreground multipart IMG routines remain outside this static BDB optimizer.

## Verification commands

```text
ctest --test-dir tmp/studio-build -C Release --output-on-failure
studio_optimizer_tests tmp/optimizer-test [path/to/MK3CAVE.BDB [--deep]]
bddview --studio-smoke tmp/optimizer-ui path/to/MK3CAVE.BDB --optimize
bddview --studio-smoke tmp/pattern-ui path/to/MK3CAVE.BDB --pattern
```

The optional local benchmark writes only to its scratch directory: a report
and a saved optimized pair for inspection. The UI smoke exercises the
background scan and captures the review; it does not apply or save the opened
game stage.
