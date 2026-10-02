# Stage optimization and pattern editing

The [current library audit](BDD_LIBRARY_AUDIT.md) compares all 68 local BDD files,
separates active build inputs from generated packs and source variants, and
records palette-preserving versus palette-copy estimates without changing the game.

The [MK3CAVE validation](MK3CAVE_VALIDATION.md) records a measured 3,096-byte
video saving through actual LOAD2 packing, complete ROM builds and 69 identical
MAME capture pairs. It also documents the origin and slot corrections needed
between an editor-verified proposal and this stage's runtime generator.

Open a paired BDB/BDD stage and select **Optimize → Find savings**. The scan
runs in the background and can be cancelled. It analyzes a snapshot; edits
made during or after analysis make that proposal stale. Nothing is written to
the game checkout by analysis or Apply.

The first implementation targets static background artwork. It searches:

- Compact palette copies for the indices actually used in each region.
- Equivalent opaque colors merged only when they match in every current palette variant.
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

**Compare full stage** opens a movable 400x254 camera preview for either a
lossless proposal or a Repeat & Mirror proposal. Use the wipe slider or toggle
between original and proposed artwork. Drag inside the view to move the camera;
both snapshots use the same camera, layer parallax and draw order. **Stage start**
resets the camera, and **Focus proposal** centers the affected artwork. This is
the document's background composition, not an emulator capture. Unbound layers
are identified as estimated; runtime actors and floor/foreground effects are
excluded. Editing the document leaves the analyzed snapshots intact and marks
them stale.

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

Lossless verification also compares each layer using the game's Z-then-Y insertion
order, testing both X arrival directions for ties. This catches a split that
reconstructs its source image exactly but changes which neighboring artwork covers
it. A failed or unavailable check disables **Apply verified proposal** and identifies
the affected layer. Try a different source image or fewer pieces; a combined scan
is refused if any part fails this check. Shared-base and pattern-packing proposals
use the same verifier. Deliberate pattern edits are compared against their intended
edited artwork, not against the original appearance.

Game export sorts an isolated copy's objects by X within each layer for LOAD2's
lookup tables, retaining object flags and custom cave offsets. It reopens the
serialized package and compares its runtime composition with the edited document
before allowing Apply. The export review includes this result. Ordinary document
Save keeps the authoring order. The comparison includes editor-hidden objects
because they remain in exported game data.

These are static RGB555 comparisons, bounded to 16 million pixels per layer and a
128-million-unit work budget. They do not prove every camera arrival history,
animation/actor behavior, packed pixels or object/DMA capacity. A package roundtrip
proves preservation of the **edited** scene; it does not establish that intentional
artwork changes match the previous game build. ROM receipts and emulator comparisons
remain necessary.

## Palette-aware reuse

**Find palette reuse** normalizes whole images for sharing, including X/Y flips,
without adding placements. Different index assignments can share a pixel payload
through separate compact palette copies. Duplicate opaque indices can merge only
when their RGB555 colors agree across every palette used by that image. Opaque
black remains distinct from transparent index zero.

This scan uses the total palette cap under **Search limits**. It can retain an
initial neutral canonicalization when that enables a later image to share its
payload; the final proposal must still save estimated video bytes. Empty margins
may be cropped, but this mode does not subdivide images. The regular **Find
savings** scan also tries equivalent-color normalization when compact palette
copies are enabled.

Review the palette count and palette/table bytes as well as video savings.
**Affected palettes are static** must be checked before applying a remapped
proposal: matching current RGB555 colors does not establish safety for palette
cycling, swaps, or game code that addresses particular indices. Animated/LOD
metadata and locked placements remain excluded. Preview, full-stage comparison,
Apply, undo/redo and save use the existing verified optimization workflow.

The local MK3CAVE whole-image scan modeled 203,238 → 195,036 video bytes
(8,202 saved), keeping 64 placements and increasing stored palettes from 7 to
45. Palette data increased from 764 to 3,698 bytes; it is reported separately
from video ROM. The proposal passed exact reconstruction, Apply, undo/redo and
save/reopen checks. These are estimates, not a verified game-build receipt.

## Unused-art audit

Open **Optimize → Unused art** and choose **Scan references**. With a game checkout
selected, the read-only audit collects source mentions from ASM/TBL/LOD/INC files
under `src`, `src-refactor/src` and `data`. It also reads an existing
`tmp/art_ref_graph/current.json` produced by the game's reference-graph tooling.
It does not run the game tooling, assemble a build, or write to the checkout.

The table shows image and palette placements, image-default palette references,
animation/LOD metadata protections, duplicate palettes, source file/line evidence
and graph label matches. Hidden and locked placements count as uses. Turn off
**Only assets without placements** to inspect all entries, then select a row for
its evidence. Reports can be copied or saved, and a selected image can be viewed
in Assets. Document edits mark the results stale; source changes require a rescan.

An image ID alone cannot establish its assembly label. Missing mappings stay
unresolved. Source mentions can be definitions rather than consumers, and numeric
or computed references remain unresolved. Graph label matches retain every match
when a name is ambiguous; graph freshness and the mapping to this BDD are not
verified. Even a graph `DEAD` classification is a review candidate, not proof that
removal is safe. Invalid graph input or interrupted scans are marked incomplete.

The unplaced-video estimate is an upper bound for this document, deduplicated
against placed payloads and other unplaced images. It is not a promised build
saving. Palette duplicates can still have distinct runtime slot identities.
The audit therefore provides no automatic deletion or palette-renumbering action.

## Shared bases and unique details

**Find shared bases** searches different source images for common opaque pixels,
including X/Y flips and translated matches. It stores the common pixels once and
retains each image's unique details in a separate piece. Rectangular bounds may
overlap, but opaque pixels cannot overlap or erase one another. Every pixel and
every current palette variant must still reconstruct exactly before Apply.
Palette indices remain unchanged.

The search uses sampled 8x4 windows to suggest translations, then compares the
actual pixels. Quick search uses an eight-pixel anchor grid; Deep uses four.
It considers up to 1,024/4,096 source pairs and keeps up to 128 profitable candidates.
Candidates with the same canonical common base can join a family of up to eight
source images. Each member retains its own details, palette assignments and
translated/mirrored placement. Families are scored together, counting the base
once. Pair alternatives remain available if a family exceeds the remaining
placement/header limits. Each source belongs to at most one selected group.
This is bounded: family discovery depends on those profitable pair candidates
and does not search every possible common mask. Sparse common pixels may cost more to encode than they save;
those proposals are rejected. Animated/LOD and locked artwork remains excluded.

The local MK3CAVE test found pairs involving 16 source images and modeled
203,238 → 200,416 video bytes (2,822 fewer), with original pixels and palette
assignments preserved. This is a separate proposal from the regular cut/palette
scan. Do not add their savings figures together: apply a chosen proposal and
scan the resulting document again to explore combinations.

## Savings map

The **Savings map** displays the latest verified scan over its source stage.
Yellow shows removed blank regions, blue lower-BPP regions, purple shared
pieces, orange mirrored reuse and gray retained unique details. Filter by type,
zoom with the wheel, pan with the right mouse button, and click a region to
open its source image in the proposal review. Shapes describe region bounds;
the colors are not additive byte savings. A stale map remains a labeled
snapshot until a new scan completes.

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
- **Discover sizes & mirrors** searches multiple group sizes, source offsets,
  alternating flips and both mirrored sides on the selected X/Y axis. It works
  on one image or a whole layer. Select a result to load its controls, preview,
  difference counts and packing proposal; use **Compare full stage** before Apply.
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

### Automatic discovery

Discovery runs in the background and can be cancelled. It considers aligned
sizes from powers of two, divisions into 2–16 groups and the current group size,
bounded between one sixteenth and one half of the source extent. Each repeat
variant samples up to roughly 34 source offsets. Ranking uses up to 4,096
stratified pixel samples, with extra weight for silhouette changes. Up to twelve
shortlisted candidates receive full-resolution packing and verification.
Cancellation completes after the current packing pass.

The result list shows only candidates that reduce estimated stage video bytes.
It retains tradeoffs in bytes, changed pixels, silhouette changes and placement
count, ordered by fewest changed pixels first. All displayed change counts are
full-resolution counts across every current palette variant; they are not sample
estimates. **Exact appearance** appears only when no rendered pixels change.
Other results deliberately alter artwork and require visual review. The search
does not guarantee the best partition or an aesthetically acceptable result.

The local MK3CAVE spike-layer check sampled 1,072 variants, packed ten candidates
and retained six tradeoffs. Mirroring the left half modeled 203,238 → 200,602
video bytes, with 7,606 changed pixels and 64 → 54 stage placements. An 80-pixel
alternating group at offset 288 modeled 203,238 → 186,360 bytes, with 12,709 changed
pixels, 6,935 silhouette changes and 64 → 54 placements. These are alternative
art edits, not additive savings or measured build receipts. All six proposals
passed Apply and save/reopen comparison in a scratch directory.

On the local MK3CAVE input, repeating a 128-pixel group starting 128 pixels into
the final layer replaced 26 spike placements with 12. Stage totals changed from
64 to 50 placements and the video estimate from 203,238 to 189,504 bytes
(13,734 fewer). It changed 14,347 pixels (about 26% of the composed strip),
including 8,946 silhouette pixels. This is an example to review, not a selected
art direction or a verified packed-ROM saving. Tests wrote only scratch output.

## Visibility heatmap and scoped trims

Open **Optimize → Visibility**, enter the full camera X/Y range and choose
**Analyze visibility**. Initial bounds are suggestions from document dimensions;
they are not extracted or verified against game code. The viewport is 400x254.

- Yellow: source pixels outside the viewport throughout the selected range.
- Purple: permanently covered by opaque artwork, or covered/outside across all uses.
- Gray: protected or unresolved artwork.
- Uncolored opaque pixels: possibly visible, including coverage the scan cannot prove.

Use the overlay filter, camera sliders, mouse-wheel zoom and right-drag pan to
inspect the map. Click highlighted artwork for its source-image counts. **Compare
camera views** opens the existing original/proposed wipe comparison and identifies
the analyzed range. Moving that comparison outside the range is allowed and is
explicitly labeled: removed artwork may become visible there.

The proof is analytic over the entire continuous camera rectangle, including
fractional positions. Outside tests sweep each pixel rectangle through the range.
Occlusion is accepted only from later-drawn static, bound artwork with the same
horizontal parallax, so relative pixel alignment cannot change. Different-parallax
coverage is retained as unproven. Index zero is transparent; opaque black covers
normally. Boundary-touching pixels are conservatively retained.

Every placement of a shared image must agree before a source pixel can be removed.
Hidden, locked, unplaced, animation/LOD and unsupported image geometry is protected.
Unresolved plane bindings make the proposal review-only. The scan is bounded to
16 million source pixels and 100 million pixel visits/occlusion comparisons;
cancellation or a work-limit failure produces no applicable proposal.

Proposals clear only proved invisible pixels and crop their remaining bounds,
keeping X widths aligned to four pixels and adjusting every mirrored placement.
They add no placements or palettes. An entirely invisible image becomes a small
transparent placeholder; its image ID and placements are retained. Each accepted
trim must reduce the total modeled video payload after exact-payload sharing is
accounted for. The search is greedy and may miss jointly beneficial trims.

Before **Apply verified visibility trims**, confirm that the range covers gameplay
and that all uses of the artwork are represented by these static layers. Runtime
actors, moving/reordered layers, effects, computed image references and changed
transparency modes are not established by this proof. This preserves the stage
view under that contract, not every original source pixel or every possible camera.
Restore the original artwork or Undo before relying on a broader range later.

Apply recomputes the proof against the current document, rejects changed proposals,
and creates one undoable edit. Reports can be copied; saved pairs use the normal
save/export workflow. Byte figures remain estimates requiring a game-build receipt.
Tests compare integer and fractional camera views, XY flips, negative parallax,
shared uses and transparent holes, and exercise protection, tampering, confirmation,
staleness, undo/redo and save/reopen. MK3CAVE opened from a supported game checkout
now receives its custom runtime binding. A standalone raw input without that
checkout remains review-only for visibility analysis.

## What is proved for lossless proposals

Before enabling Apply, the verifier reconstructs every changed source image
for every palette used by its placements. It compares raw RGB555 colors and
transparency at every pixel, checks disjoint opaque coverage and bounds, and verifies
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
output, per label and orientation. The proposal estimator does not run LOAD2
or claim reclaimed ROM bytes. Use ROM receipts below for generated-output
measurements. Neither estimates nor receipts measure runtime peak objects,
palette slots or DMA usage. A larger number of
pieces can cost more runtime work even when the video payload shrinks.

The local MK3CAVE deep/balanced benchmark modeled 203,238 → 192,058
video bytes (11,180 bytes, about 5.5%), with 64 → 128 placements and 7 → 45 stored
palettes. Those are estimates for that local input snapshot, not shipped
asset data or a packed-ROM measurement. The scratch output passed complete
scene equality and save/reopen tests; the live source pair was not modified.

## ROM receipts: measured build output

Open **Optimize → ROM receipts** and choose the built game checkout. **Capture
current build** reads its literal MK2 `makevrom.py` packing declarations, the
listed generated `data/*.IRW` files and all twelve `rom/` video chips. It never
executes the packing script. The native adapter supports the reviewed MK2
layout, header bank selection and fallback mapping, bootstrap base overrides,
and continuation records. Unsupported/dynamic declarations, missing files,
truncated records, bank overflows and overlapping payloads are refused.

If `CUSTOM_VIDEO_SLOTS` is declared, capture also verifies each slotted payload's
exact base and size against the current limits. Matching IRWs and chips no longer
hide a slot overflow. The budget separates physical gaps into **unused bytes
inside reserved slots** and **bytes outside reservations**, and reports the
largest unreserved gap. Overlapping and empty reservations are counted as a union.
This uses the declared slot map; it does not prove that every remaining gap is
usable by all runtime code. **Declared ROM slots** shows asset sizes and capacities,
with a filter for names such as `MK3CV`. Slot capacity alone is not growth permission.

New receipts use version 3, saving both the slot map and static background
fingerprints. Version 1 and 2 receipts still load, with artwork comparison
explicitly unavailable; version 1 also lacks slot reservations. Checkouts without
a slot declaration retain the physical budget without inventing an unreserved budget.

Capture reconstructs the packed flat image and compares **every byte in all
twelve interleaved chip lanes**. It re-reads the inputs afterward to detect
changes during capture. A mix of stale IRWs and chips therefore fails rather
than producing a receipt. Literal `0xff` payload bytes count as occupied;
file size and non-`0xff` counts are not treated as free-space measurements.

Use the verified current capture as a baseline and save it as `.romreceipt`.
Preparing a game export automatically captures the existing build if no valid
baseline is present. Apply and Build wait for this capture to finish. If the
checkout has no packaged build, the editor explains why the baseline is unavailable;
it does not invent a comparison. After applying your reviewed export and building
the game, capture again.
A successful build launched through bddtool starts a new capture automatically;
view its result under ROM receipts. Save/load controls preserve the baseline
and comparison between sessions. Receipts store counts, ranges and change
identifiers, not game artwork or ROM contents.

For MK7 backgrounds, capture decodes each active stage's generated image headers
against the verified IRW payload and records indexed-pixel and dimension
fingerprints alongside the exact BDD/BDB source fingerprints. Relocation alone
does not count as an artwork change. If a stage's source is unchanged but its
decoded images differ, **ROM receipts** and **Build & Check** show a regression.
Changed, added or removed stage sources require visual review. Missing inputs or
unsupported tables leave artwork coverage unavailable without discarding a valid
byte-budget receipt. Source pairs, the LOD and headers are also re-read to detect
changes during capture.

The comparison reports actual packed payload bytes, physical free bytes and
largest contiguous gap per bank, plus per-IRW changes and relocated payloads.
These are **whole-build** measurements: other edits can contribute to the delta,
and physical gaps may be reserved by the game's slot policy. A historical
receipt records verification at capture time, not the current state of files.
Change identifiers are noncryptographic; receipts are not signed attestations.

Matching IRWs and chips can both be stale relative to edited sources. Manual
captures therefore leave source freshness unproven. Program-ROM tables and
palette bytes remain separately labeled document estimates. Static comparison
covers indexed pixels and dimensions, not palette appearance, IMG animation,
runtime bindings, edited-stage equivalence or object/DMA peaks. Those require
separate checks. No emulator or optimized game build is run by the optimizer tests.

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
can broaden subregion sampling and add global palette-family planning. Runtime actors and
foreground multipart IMG routines remain outside this static BDB optimizer.

## Verification commands

```text
ctest --test-dir tmp/studio-build -C Release --output-on-failure
studio_optimizer_tests tmp/optimizer-test [path/to/MK3CAVE.BDB [--deep]]
bddview --studio-smoke tmp/optimizer-ui path/to/MK3CAVE.BDB --optimize
bddview --studio-smoke tmp/pattern-ui path/to/MK3CAVE.BDB --pattern
bddview --studio-smoke tmp/shared-ui path/to/MK3CAVE.BDB --shared
bddview --studio-smoke tmp/review-ui path/to/MK3CAVE.BDB --optimize-review
studio_rom_receipt_tests tmp/receipt-test [path/to/built/game]
studio_visibility_tests tmp/visibility-test [path/to/unbound/MK3CAVE.BDB]
bddview --studio-smoke tmp/visibility-ui path/to/stage.BDB --visibility
```

The optional local benchmark writes only to its scratch directory: a report
and a saved optimized pair for inspection. The UI smoke exercises the
background scan and captures the review; it does not apply or save the opened
game stage.


## Animated art: Forest and IMG libraries

**Optimize > Animated art > Analyze Forest sources** loads the recognized Forest
sequence from the checkout selected in Build & Check and decodes its frames from
`data/MKBGANI.IMG`. The scan runs in the background and can be cancelled. It reads
local sources only; it does not modify the document, IMG, LOD or assembly files.
Other runtime animation drivers remain unsupported and show an explanation.

**Choose IMG frames...** also opens any local IMG library for manual analysis. Filter
labels, select individual records or **Select matches**, and choose a preview duration
(1..60 ticks at 60 ticks/second). Up to 128 records and two million decoded pixels
can be selected; frames must fit within 1024 pixels on each axis. **Edit selection**
reopens the picker. All five searches operate on the selected artwork, including
libraries unrelated to the current stage.

This mode plays selected records once per loop in **IMG directory order**, preserving
their signed frame anchors. It does not infer an assembly animation sequence,
sequence opcodes, multipart composition, actors, timing, palette swaps or the game
driver. The comparison and copied report identify manual timing/order explicitly.
A multipart component selected by itself is analyzed as a component, not a complete
pose. Directory entries with ambiguous labels, missing default/external palettes,
invalid dimensions or invalid offsets are unavailable. Selected pixels and palettes
are validated again when loading; decode failures produce an explanation. The
manual comparison never replaces the Stage runtime-animation overlay or enters
BDD/BDB save/export. Analyze again to reread changed IMG files.

Five independent alternatives are compared:

- Trim transparent margins and reuse identical or X/Y mirrored whole-frame payloads.
- Compact indices with one mapping for each source-palette family across all its
  frames, then reuse frames. Distinct authored indices stay distinct, including
  opaque black versus transparent index zero. This does not assume a separate
  palette can be installed for every frame.
- Share opaque pixels that have the same index at the same actor-relative position
  across every frame in a palette family; store the remaining details per frame.
  The shared base excludes any pixel that disappears or changes in another frame,
  so detail pieces never need to erase a base pixel.
- Selectively share bases between groups of related frames, leaving unrelated poses
  intact. The search starts with whole frames and greedily merges profitable groups.
  A merge must reduce both the entire animation's deduplicated video estimate and
  its video-plus-frame-description estimate, including existing reuse. Groups never
  cross source palettes. Limits are eight frames per group, the first 32 frames,
  128 candidate evaluations and 32 million input-pixel visits across evaluations;
  the UI/report says when the bounded search reaches a limit. This is not an
  exhaustive optimum and does not yet search moving components or arbitrary cuts.

- Share horizontal bands whose complete opaque rows match between frames at the
  same actor-relative position and with the same source palette. Runs must be at
  least eight rows long. The search considers two cuts together, so it can isolate
  a reusable middle section while keeping different top and bottom artwork. It
  evaluates whole-animation video and frame-description costs before accepting a
  split. Limits are eight pieces per frame, the first 32 frames, the 512 longest
  proposed bands, and 128 candidate/32-million-input-pixel work caps. Greedy choices
  and limits are reported; translated or mirrored row-run discovery is not included.

Each frame is reconstructed and compared with the original RGB555 colors and
transparency at its signed animation anchor. Overlapping opaque pieces are rejected.
Both playback panels use the original sequence and tick duration with a common
view scale and origin. The blue cross is the actor anchor. Sequence repetitions and
multiple actors do not multiply stored artwork; the Forest's seven frames are used
in twenty sequence steps by three trees. As in the Stage preview, random idle pauses
are omitted. Pause or scrub to inspect individual steps.

Video figures use the existing background auto-BPP/zero-compression estimate with
16-bit alignment. **Both baseline and proposals receive the same four-pixel width
padding**, so a nonaligned source cannot falsely appear to save space just because
a padded proposal becomes eligible for the model's compression path. These figures
are not the actual animation LOD packing or measured ROM receipts. Palette bytes
and assumed frame descriptions (14 bytes per piece plus 4 per frame) are separate;
runtime code size and object RAM remain unmeasured. A negative saved-byte figure
means the alternative would be larger under the model. Alternatives are not additive.

The local Forest check found a 31,690-byte baseline estimate. Whole-frame reuse and
family palette compaction produced **no video savings**. Family palette entries
fell from 53 to 50, still requiring the same bit depth. The all-frame stationary-base
proposal grew to 32,950 bytes and needs two pieces per actor. This is a useful
rejection of an unprofitable split, not a recommendation to alter Forest. The
selective search also checked all 21 frame pairs without a profitable merge and
retained the whole-frame representation. The horizontal-band pass found no runs
of eight matching rows in Forest, so it also retained the whole frames.

**Copy analysis report** includes per-frame image/palette references, signed offsets,
flips, shared/detail roles, group membership, search counts, and actor-relative band
cut positions. **Piece outlines** shows shared pieces in purple and frame details in
blue. The compact layout keeps the previews visible, with model/runtime notes under
**Estimate details and runtime integration**. Results describe the loaded source
snapshot; run the
scan again after changing source files. No Apply/export action is exposed: Forest
currently creates single-frame DMA objects through `make_a_mad_tree` and advances
three actors through `triple_framew`. Multipart artwork, flip changes, and palette
remaps need a reviewed consumer adapter, checks of every shared/alternate/cycling
palette use, decoded packed-art comparison, and in-game validation first.

The implementation follows the consumer constraints in the game checkout's
`doc/graphics_opportunities/MULTIPART_SHARE_SCAN.md` and
`PALETTE_REINDEX_SCAN.md`. It does not treat their older measurements as current
build receipts. Tests cover anchor changes, XY reuse, shared-base holes, opaque black,
family palette ownership, odd-width budget consistency, malformed data, cancellation,
unchanged sequence/source ownership, subset-only savings, profitable three-frame
growth, protection of existing whole-frame reuse, search limits, and deterministic
results. `--animation-analysis` runs the UI smoke with a Forest BDB and exercises
all five alternatives at normal and compact sizes, including piece outlines. Tests
also verify two-cut middle bands, signed Y boundaries, differing palette ownership,
actor-relative X mismatches, and preservation of existing whole-frame reuse.


`--animation-library` exercises a manual TREEANI selection from the local MKBGANI
library with a seven-tick preview, opens/closes the filtered picker, and verifies
that the recognized Forest runtime overlay retains its own sequence and timing.
Core tests cover manual ordering/repetitions, signed anchors, palette availability,
duplicate labels, invalid selections/timing, report provenance, and unchanged stage
ownership. The catalog reads directory metadata only; analysis decodes selected art.
