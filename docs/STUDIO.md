# bddtool — first implementation

The application now opens in a new stage placement workspace. The existing
format code is shared by the new workspace, the original editor, and the `bddtool` CLI. The
[redesign audit](UI_REDESIGN_AUDIT.md) remains the architectural assessment of
the pre-rebuild code.

## Run

Build with the existing CMake workflow, then launch:

```text
bddview                       # new workspace
bddview path/to/stage.BDB      # opens its BDD companion too
bddview --studio-demo          # synthetic, editable sample stage
bddview --legacy-ui [file.BDB] # original specialist tools
```

Opening a standalone BDD displays its asset library. Create a layer to begin
placing its artwork. Several stages can be open simultaneously; each has its
own selection, camera, undo history, and save point.

## Arrange a stage

- Drag artwork directly on the canvas. Shift-click adds to the selection;
  dragging empty space makes a selection rectangle.
- Select a **layer row in the tree**, then drag its artwork to move the layer
  as a unit. Select an individual child to move one piece.
- Drag a thumbnail from the asset tray into the selected layer. Import accepts
  PNG, TGA, BMP and selected IMG artwork. Raster imports use transparency below
  alpha 128 and exact RGB555 colors. IMG imports preserve indexed palettes.
- Use arrow keys for one-pixel nudges, Shift+arrows for ten pixels. Hold Shift
  during a drag to constrain movement to one axis. Ctrl+D duplicates selected
  artwork; Delete removes it.
- Use the wheel to zoom around the pointer, including below 100%. Middle-drag
  or Space-drag pans. **Fit** / Ctrl+0 frames the stage or camera.
- Snap is enabled initially. Alt temporarily disables it during a drag.
- Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y redoes, and Escape cancels a drag. Each
  completed drag is one history entry.
- Use the inspector for positions, flips, palettes, layer assignment, layer
  order and parallax. Numeric position/name edits commit with Enter.
- Layer visibility, locks and solo are available in the tree and inspector.
- **Source layout** exposes the packed source coordinates. Switch back to
  **Stage composition** to drag artwork.

The canvas renders and picks the same live document. It does not draw an
external BLKS table while editing a different set of placements.

## Import and edit artwork

Use **Assets → Import PNG / IMG**, the Assets toolbar, or drop a file onto the
application. IMG opens a filtered selection dialog; selected images enter the
asset library as one undoable batch. Drag them into a stage layer to place them.

For multiple raster files, choose **Import → PNG / TGA / BMP folder**. Review
the selection and palette counts before importing the whole batch. Identical
palette reuse can be disabled. The folder workflow uses the reviewed decoded
data and never partially imports a failed selection.

Double-click artwork for **Edit block**, or use the thumbnail context menu or
placement inspector. Paint, erase, pick colors with right-click, flip X/Y and
undo individual strokes in the draft. **Apply block** creates one document
history entry; **Cancel** discards it. Scrollbars navigate enlarged blocks.

Choose **Find subframes...** from selected artwork, its inspector, or the asset
context menu to open a focused Optimize review. It searches transparent margins,
blank internal corridors, repeated regions and X/Y mirrored regions for that
image. Every placement and palette variant is rebuilt together. The review shows
the original beside an exact reconstruction, cut boundaries, BPP, modeled video
bytes and added placement-table cost. Apply is one undoable edit; unchanged or
unverified proposals cannot be applied. **Scan whole stage** returns to the
broader lossless search. Focused structural scans preserve palette indices by
default; palette-aware reuse remains a separate reviewed action because runtime
code can cycle or swap those indices.

**Palette** edits the selected shared palette with RGB555 precision. Copy the
palette for that artwork first when other images should retain their colors.
The Color adjustments section previews brightness, contrast and saturation for
the whole palette using the exact RGB555 result. **Blend toward** interpolates
matching color indices toward another document palette without changing any
artwork indices. Reset returns to the stable
baseline; use the current colors as a new baseline before deliberately stacking
adjustments. Index zero remains transparent. Palette shrinking and painting
reject missing color references. **Export PNG**
saves one image using its active/default palette. See the
[migration checklist](ASSET_TOOL_MIGRATION.md) for limits and remaining tools.
That guide also records which old panels are being consolidated or retired.
Safe bookkeeping belongs to Save: counts, companions, metadata and backups are
automatic, and the temporary BDB/BDD are now read back and checked before file
replacement. Art-changing cleanup remains a deliberate editing/Optimize action.

The palette editor can import and export `.rgb555` palette files. The text format
stores the palette name, color count and exact four-digit RGB555 words. Import
loads a review draft and updates the swatches, but does not modify the document
until **Apply palette**. Unsupported formats, malformed words, extra data and
files over 16 KiB are rejected.

**Assets → Export artwork sheet** creates a transparent PNG containing either
the full BDD artwork library or only images used by visible placements. Choose
the column count and cell padding; images are centered without resampling. A
companion JSON file records each image ID, label, palette, cell and exact pixel
rectangle. When an image is visibly placed, its first resolved placement palette
is used; otherwise its saved default palette is used.

**Export TGA** on the selected asset writes an 8-bit indexed, color-mapped TGA.
It preserves every pixel index and the palette's RGB555 words, including the
high flag bit, so external indexed-pixel edits can round-trip without flattening
the image into RGBA. The exporter refuses incomplete pixels and indices outside
the selected palette.

## Background color

Open **Stage → Background...** to choose colors with the picker or enter a hex value.

- **Editor canvas** changes the workspace background for this session. Dark,
  Black and White presets help inspect artwork; this setting does not edit the map.
- **Set stage color** previews a backdrop inside the 400×254 game frame in
  Stage composition and Camera preview. **Apply stage color** makes one undoable
  edit; **Cancel stage change** discards the stage draft. Colors use the game's
  RGB555 precision. Save keeps the color in the map's `.bddstudio` companion.

Supported MK2 exports include the backdrop in the reviewed assembly changes.
Uncheck **Set stage color** and Apply to preserve the game's existing color on
export. MK3 layouts can import, preview and save their backdrop, but MK3 game
export and custom stage generators do not yet support backdrop edits. The cave
exporter refuses a set backdrop instead of discarding it. Shared overview and
camera PNGs include the saved backdrop; individual prop PNGs keep transparency.

Old layout companions remain readable. A map with a set backdrop uses companion
version 3, so keep that file beside BDB/BDD and open it with an updated bddtool.

## Floors

Use **Stage → Floor...** for floors missing from the map. It offers two paths:

- **Existing game floor:** supported external floors load automatically from
  the chosen game checkout or when applying an MK3 game layout. Toggle **Game floor reference** on Stage, or use
  **Load / reload game floor** after changing the checkout or floor sources.
  The reference is excluded from saving, picking, optimization, sharing and
  ROM budgets. Source layout and layer solo hide it.
- **Add editable floor artwork:** choose a PNG/TGA/BMP or **Use selected asset**.
  For IMG artwork, import/select it in Assets first. Set left X, top Y and
  width; narrower widths crop, wider widths repeat without scaling. **Add floor
  to map** creates a Floor layer and its artwork as one undoable edit. Save
  includes these pieces. Adjust position and layer order normally on Stage.

You can also use a supported game-floor texture as the source for new static
artwork. Adding artwork hides the reference to avoid displaying both copies.
It does not replace the game's external floor or its runtime code. The new
layer needs explicit runtime integration before it can appear in the game.
Custom stage profiles such as MK3CAVE still manage their floors separately.

The MK2 reference loader matches source module names to a stage in `BGND.ASM`,
reads its literal floor descriptor/palette and `data/FL_*.BIN` texture, and
uses its display-list order. This first version supports 1200-pixel raw 6bpp
rows. Unsupported, missing or ambiguous data is reported in the Floor dialog.
The floor is a static start-position reference with 1x camera scrolling;
perspective skew, skipped rows and palette effects are not simulated.

For MK3, select **MKBT.ASM** in **Game layout...** and apply the layout. The
floor loads from its stage descriptor, using `FL_*.BIN` in the map's `BINFILES`
folder (or beside the map/source file, or in the checkout's `data` folder).
Palettes come from `MKBT.ASM` or the adjacent `BGNDPAL.ASM`. The reference uses
the source display order and floor scroll rate, starts 400 pixels into the
1200-pixel texture, and aligns its bottom with the 254-pixel game frame at the
start camera. Original palette index zero stays opaque. Perspective skew,
palette animation and stage callbacks are not simulated. Missing floor data
is explained in **Floor...**; it does not prevent applying the module layout.
The floor stays hidden until its layers have runtime bindings. After a restart,
select the source again if it is not adjacent to the map or in the checkout.

**Floor... → Browse IMG floors...** opens a searchable floor library. It finds
nearby `MKFLOOR*.IMG` files beside the map, in the selected checkout's `data`
folder, and beside a library you explicitly open. Use the library dropdown to
switch between files such as `MKFLOORS.IMG`, `MKFLOOR2.IMG` and `MKFLOOR3.IMG`.
**Refresh list** rescans those folders; **Open IMG...** selects any other library.
The scan does not recurse or search other checkouts. Hover a filename to see its
full path when multiple folders contain the same name.
All entries are included, even artwork not used by the map;
IMG files do not prove whether an image was used in a shipped game.

Numbered strips such as `SUBFLOR1`–`SUBFLOR6` form one suggested floor choice,
joined left to right with their bottoms aligned. Individual unnumbered images
remain separate choices. Missing/duplicate strip numbers, missing palettes and
unsupported data are reported rather than silently assembled. Each strip keeps
its colors, including opaque stored color zero; trimmed margins stay transparent.

Each choice has a clickable thumbnail, dimensions and strip count. Transparent
areas show a checkerboard. Scroll or search to browse visually; thumbnails load
as rows become visible. Hover an unavailable preview for its error and strip
details. Reopen the IMG to refresh thumbnails after editing it outside bddtool.
Thumbnails are reduced previews; the selected stage reference uses full-size art.

Selecting a floor changes the reference immediately. Close the library window
to see the stage, then use its new floor dropdown or **< / >** buttons to cycle
through the matching choices. **Game floor** or **Restore game floor** returns
to the runtime texture. Alternatives retain its bottom position, scrolling and
draw order; without a runtime floor they are centered and bottom-aligned in the
400×254 frame with 1x scrolling. This is a static comparison, not proof of the
original strip assembly or runtime behavior. Choices last for the current tab
session and do not alter Save, exports, ROM budgets or game sources.

Below the preview controls, a pixel comparison reports an **exact pixel match**,
different dimensions, or the number of different pixels against the loaded game
floor. It compares colors and transparency, so palette renumbering does not
create a false difference. A changed or unavailable runtime source disables the
comparison until reloaded. This identifies a texture match only; differences do
not establish whether an alternative was unused in the game.

**Use as editable artwork** transfers the preview into the existing floor draft.
Set its placement/repeat width and click **Add floor to map** to make an undoable
edit. Source IMG files remain unchanged. Composites are limited to 4096×254,
128 strips and 255 opaque RGB555 colors; color reduction is never automatic.

## Camera and runtime interpretation

On opening an unmodified stage, bddtool reads runtime plane offsets, camera
origins, scroll factors and draw ranks when the stage has an adjacent draft or
is inside a game source tree with `src/BGND.ASM` or
`src-refactor/src/BGND.ASM`. This interpretation reuses the existing parser
behind a read-only adapter. No machine-specific checkout is selected as a
fallback. Missing bindings retain their source placement and produce review
messages under **Build & Check**.

Stages with runtime bindings open in camera preview automatically. The camera
frame is 400×254; areas outside it are shaded. Scrub the camera horizontally or
change its Y coordinate, return to **Start**, or choose **Set start**. Layer
editing remains available in camera preview.

This is an authoring preview. Forest tree faces are supported as described below.
Other runtime actors, animated palette effects, game-specific floor deformation,
and compiled ROM output are not reproduced or emulator-verified.
Inferred runtime bindings do not establish complete
game fidelity. Stages opened without runtime source need their layer positions
arranged manually in this first implementation.

## MK3 artwork archives and the game frame

An artwork-only archive such as `deadlythirdcombat-main` supplies BDB/BDD
source sheets, not the camera and layer arrangement used in the game. Without
runtime bindings, the rectangle is now labeled **Frame guide (layout not
loaded)** and Stage explains why the artwork is still at source-sheet positions.
Changing the frame dimensions cannot recover the missing layer transforms.

Use **Stage → Game layout...**, browse to the matching MK3 revision's
**MKBT.ASM**, review the detected stage and layer transforms, then **Apply game
layout**. The importer handles MK3's camera-Y/ground-offset header order,
per-layer `center_x` initialization, module offsets, parallax and display order.
It matches source module names, preferring the definition covering the most
modules; equally complete matches are refused. Centering uses the current
artwork's tight module bounds, not potentially stale compiled BMOD tables.

Apply is one undoable edit. It preserves pixels and placement source coordinates,
switches to the imported start camera and fits the game frame. Save preserves
the resulting transforms in `.bddstudio`. An optional session setting reuses
the selected definition file when opening other maps from the same asset folder;
existing saved layouts remain authoritative. For original source trees,
`MKBT.ASM` beside the art or in the checkout's `src` directory loads on open.
The MK2 custom MK3CAVE adapter is used only when its generator exists.

This imports static module layout and an optional floor reference, not MK3
build/export support or runtime actors, callbacks and palette effects. Unassigned artwork remains at
its source position. Missing, ambiguous or unsupported definitions leave the
document unchanged. The local MK3 V13 scan matched 29 of 47 archive BDBs; the
other 18 had no matching module definition in that source revision. No archive
files were saved during testing.

## Forest animation preview

Forest stages automatically show the tree-face animation when their checkout
contains `data/MKBGANI.IMG` and a supported Forest definition in `src/BGND.ASM`
(or `src-refactor/src/BGND.ASM`). The loader matches the Forest's source modules,
reads its actor spawn positions, insertion list, frame table and frame duration,
then decodes the referenced IMG frames with their palettes and signed animation
offsets. It does not assume the filename must be `FOREST.BDB`; `FOREST2` works too.

Use **Animations**, **Pause/Play**, the arrow buttons, or the sequence slider
above the canvas. Stepping pauses playback. Each tab owns its animation assets
and playback position. Source layout and layer solo hide the actor overlays.
The preview repeats the roar at 60 preview ticks per second, using the source's
ticks-per-frame. It deliberately omits the game's randomized idle pauses and
does not claim exact emulator timing.

The faces remain at their source-defined positions while artwork moves, so
you can align the trees around them. They are read-only overlays: picking,
undo, save, the asset tray and game export still operate on authored BDB/BDD
artwork. Animation frames never get baked into the background. Layer export
preserves actor-only display-list slots while rearranging background entries.

After changing the checkout or editing its assembly/IMG externally, use
**Build & Check → Reload animation sources**. This also works for files with
an existing `.bddstudio` layout. A changed checkout hides the old overlay until
reloaded. The panel identifies the source and explains missing, malformed or
unsupported data instead of substituting guessed frames. The selected
checkout's assembly is authoritative for animation; adjacent draft assembly
used by the legacy plane importer is not used for actor preview.

## Save and recovery

The **Optimize** workspace adds background analysis, cut/reuse previews,
palette and placement budgets, exact pixel verification and an undoable Apply.
Its **Repeat & Mirror** workshop also previews deliberate artwork changes,
including repeated groups across a whole layer and mirrored pillar sides.
Apply combines the art edit and tile reuse in one undo step.
Shared-base scans preserve unique details, the savings map links regions to
their proposals, and full-stage comparison follows the document camera.
**Optimize → Palettes** finds byte-identical RGB555 palettes and previews their
slot remap, placement/default references and exact palette-data savings. It can
run the existing game-reference audit in the same workspace. Applying requires
confirmation that palette-slot, cycling and swap references were reviewed,
because identical current colors do not prove external code treats two slots as
interchangeable. Apply is one undoable edit. Near-color merging, unused-palette
deletion and palette unions are excluded from this exact pass.
ROM receipts capture and compare verified packed output from existing builds;
successful builds launched here start a capture automatically.
See [Stage optimization and pattern editing](OPTIMIZE.md) for scope, controls and the
distinction between estimated video savings and verified packed-ROM savings.

Save writes the BDB/BDD pair, BDD image metadata, and a `.bddstudio` layout.
Keep that layout beside the pair when moving the project. It holds layer
transforms, display order, camera start, visibility, locks and asset palette
defaults, with content hashes tying it to the pair. Source ownership is
explicit while editing. If artwork crosses its original module rectangle,
save repacks source rectangles and adjusts exported layer origins to preserve
the visible scene. Existing unassigned artwork may need a layer assignment
before a repack can be saved safely.

Normal Save does not write shared game assembly. Use the explicit game export
workflow below to apply the layout before building. The original editor does not understand `.bddstudio`
layer transforms; use it for its specialist operations, then review bddtool's
layout warning if it has changed the pair.

Each save prepares all output files and backups before replacing any targets.
Existing files receive `.studio-backup` copies. A failed replacement attempts
rollback; `.bddstudio-saving` records identify an interrupted transaction.
Opening a pair with an outstanding journal is refused. Restore the listed
existing targets from their backups (and remove newly created targets listed
as previously absent), then remove the journal after recovery. This is not a
filesystem-wide atomic transaction or a guarantee against power-loss damage.

Dirty documents get recovery copies approximately every minute under SDL's
application preferences directory (`%APPDATA%/midway-bddtool/studio/recovery`
on Windows). **File → Recovery copies** lists them. Opening a recovery copy
and using **Save as** keeps it; recovery never marks the original document
saved. Copies are retained for manual cleanup. Unsaved tabs prompt before
closing or quitting.

## Apply the layout to an existing game stage

Open **Build & Check** and use the game integration controls:

1. Choose the game checkout. bddtool detects it when opening `data/STAGE.BDB`
   beneath a folder containing `src/BGND.ASM`.
2. Click **Prepare game export**. This takes a snapshot of the current live
   document, including unsaved edits, and creates a new export package. It
   does not modify the game checkout or mark the document saved.
3. Review the report. **Copy export folder** locates `REVIEW.txt`, `BGND.diff`,
   the BDB/BDD pair, metadata, `.bddstudio`, and the patched `src/BGND.ASM`.
4. Click **Apply reviewed export**. bddtool checks that the document revision,
   selected destination, staged files, and existing game sources still match
   the reviewed snapshot. It backs up the five target files before replacing
   them. Other source files are not part of this apply transaction.
5. Click **Build game** to run that checkout's `build.py` with Python, in the
   checkout directory, without invoking a command shell. The editor stays
   responsive and displays the log and exit status. This is the full build,
   including LOAD2, not an assembly-only shortcut. The checkout's build script
   controls its generated-source updates and external toolchain requirements.
6. Use the game's normal ROM packaging and emulator workflow to verify it.
   A successful build is not a claim of visual parity in MAME.

The export report includes declared ROM slot capacities for the LODs referencing
the stage. Their packed sizes remain unknown until LOAD2 runs. Referenced LOD
files and `makevrom.py` are read dependencies: Apply and Build recheck them, including a
previously absent `makevrom.py`, and asks for a fresh export if they changed.
They are never installed as package output. Capture a ROM receipt after the
full packing build to check actual slot bases and sizes.

MK3CAVE uses its own generator for original-coordinate moves, packed water rows
and multiple ROM packs. Open its BDB from a supported game checkout to bind that
runtime automatically. The cavern/water layer starts locked, and optimization
preserves the seven runtime palettes. Its version-2 `.bddstudio` sidecar retains
source-coordinate moves; saving keeps the original module rectangles.

For this stage, **Prepare game export** creates the source pair, four ROM pack
pairs, a packing manifest, a generator adapter and a build helper. Review the
per-pack estimates against the checkout's current reservations, then apply with
the same backup and stale-source checks. Static artwork and layer offsets can
change; this first export profile requires the water artwork, palettes, camera,
floor, layer order and parallax rates to remain intact. An old unbound layout
must be reopened from the original-coordinate source before editing.

**Build & verify ROMs** runs the full game build, verifies every decoded cave
pixel and pack boundary, rebuilds water spans, and packages `rom/bddtool/mk2.zip`
without installing it. A successful run writes `tmp/bddtool-cave/verification.json`.
Existing game-build guards still apply. The current validation snapshot stops
at an unrelated Cage logo consistency guard; separate cave pixel and emulator
checks passed. See [MK3CAVE validation](MK3CAVE_VALIDATION.md) for that distinction.

Packages live under the application's preferences directory in `exports/`.
Each has a unique directory and keeps its original backups. `APPLIED.txt`
records a completed apply; `APPLYING.txt` identifies an interrupted transaction
and lists the originals to restore. Failed replacement attempts roll back.
The multi-file apply is not atomic across a machine crash. Do not remove an
unfinished journal until its listed files have been recovered.
A checkout-level `.bddstudio-applying/RECOVERY.txt` points to the package
when an apply is interrupted. bddtool refuses further exports to that checkout
until recovery is complete and the marker is removed.

The exporter updates the chosen stage's plane offsets, parallax table,
relative layer draw order, camera start and ground. It accounts for LOAD2's
tight artwork bounds and `center_x`, including bounds changed by moving an
object. Rates and offsets are rounded to the game's fixed-point/integer
precision. Camera comparisons allow up to one pixel of rounding.
The runtime import adapter now compensates for those same tight bounds.
Display-list import also recognizes additional actor lists such as fighter
afterimages, so background layers following them retain their correct order.

The game display-list slots occupied by fighters, shadows, floors and actors
stay in place; background-plane entries are permuted through the existing
background slots. Moving a background across such a slot can change which
layer appears in front of fighters. Stage actor routines and unrelated stage
definitions are preserved. Visibility, locks and solo remain editing aids and
do not remove artwork from the exported game.

This integration updates existing stages. It requires a matching pair in the
game's `data/`, a DOS-compatible stage name of at most eight characters, and a
`BBB>` reference in `data/*.LOD`. The full game build must actually consume
that LOD and promote its regenerated tables. For the tested NUPOOL checkout,
`build.py` already performs complete BLKS/BMOD promotion. bddtool does not
invent ROM allocation or promote unverified assembly skeletons.

Module names identify the stage definition. If several match, enter an exact
label, such as `dedpool_mod`. Ambiguous/unsupported initializers, shared scroll
or display-list tables, missing runtime modules, empty runtime planes,
unassigned artwork, and newly added modules without integration stop export
with an explanation. Existing unused source layers are reported explicitly as not shown in game; their source artwork and local transforms remain in the exported project.
Adding a new runtime plane or stage slot still needs the specialist tools.
Existing image-width concerns appear in the review report for LOAD2 checking.

## Implementation boundary

- `bdd_core`: existing format and metadata routines, shared as a static library.
- `bdd_studio_document`: owning state, immutable shared image banks, explicit
  layer ownership, scene projection/picking, edit transactions and save logic.
  It has no SDL, ImGui, or legacy global-array dependency.
- `platform/UI/studio`: workspace, rendering and the temporary read-only
  runtime parser adapter. The original UI and headless CLI remain available.
- `studio_game_export`: pure assembly transformation plus reviewed package
  preparation/apply, conflict detection, backups and rollback.
- `studio_game_build`: asynchronous, shell-free launcher for the selected
  checkout's build script, with captured output and exit status.

History retains up to 64 edits. Raster imports require at most 255 opaque
RGB555 colors. Pixel/palette editing and selected IMG import are in Assets;
advanced palette reduction/grouping, IMG-folder/LOD import and specialist exports
still use the original editor. See the asset migration checklist above.
**Build & Check** begins with a searchable findings list, with severity/category
filters and a copyable report. It checks references, layer assignment, image
storage/IDs, palette counts and pixel-index ranges (including alternate placed
palettes), runtime bindings, and LOAD2 width/capacity constraints. Repeated
placements share image-level findings. Open artwork or inspect the affected
palette directly; Locate placement selects and frames the piece in Stage.
Checks include hidden/unplaced art and refresh after edits/undo. They do not
repair files or establish in-game equivalence. See the [workflow decisions](ASSET_TOOL_MIGRATION.md)
for exact coverage and remaining diagnostics.

**Scan camera range** adds sampled object/palette pressure and horizontal gaps
to the same findings list. Expand **Camera range and assumptions** to adjust X/Y
bounds, step and the reserve for actors/fighters/effects. **View camera** opens
the flagged sample on Stage. Hidden placements count because they are exported;
the editor does not unhide them. These are bounds-based estimates, not pixel
coverage or gameplay verification. Document/range changes mark the scan stale;
rerun it to refresh findings. Scanning never saves or builds anything.

After preparing an export, **Recheck export files** compares source, staged
output and reviewed dependency bytes. After Apply, it checks installed output
and dependencies. Findings appear under **Export / sources**, with a link to
the export controls. Checks also run after Prepare/Apply; hover the last-check
status for its timestamp. Document, checkout or stage-label changes require
a matching review. Build game rechecks files immediately before launching.
These observations cover the reviewed inputs only; they do not establish
generated-table or ROM freshness. See the [export/source check details](ASSET_TOOL_MIGRATION.md#exportsource-checks--october-5-2026).

MK3CAVE additionally supports the verified packing workflow above; other stages
use the game's normal packaging tools.

## Help and sharing

Help contains **About / build information**, wiki and stage-catalog links, and
editing shortcuts. Use **File → Share stage** or **Help → Share a stage** to
prepare a local ZIP from the current applied edits. Enter a bundle/wiki name,
author, credits/use terms and testing notes, then review the generated files.

The bundle includes BDB/BDD, editor layout and metadata companions, static
previews, props and a wiki page. **Open GitHub submission draft** opens the
browser; attach the ZIP and submit it yourself. Accepted submissions use the
repository's maintainer approval workflow. The app never posts automatically.
Sharing preserves your current save point and undo history. Changed document
contents or credits require rebuilding before submission. See the
[sharing details and limits](ASSET_TOOL_MIGRATION.md#help-and-stage-sharing--october-5-2026).

## Verification

```text
ctest --test-dir build -C Release --output-on-failure
bddview --studio-smoke tmp/studio-ui
bddview --studio-smoke tmp/checks-ui --demo --checks
bddview --studio-smoke tmp/camera-checks-ui --demo --camera-checks
bddview --studio-smoke tmp/export-checks-ui --demo --export-checks
bddview --studio-smoke tmp/floors-ui --demo --floors
bddview --studio-smoke tmp/mk3-layout-ui --demo --mk3-layout
bddview --studio-smoke tmp/mk3-layout-real path/to/MAP.BDB --mk3-layout path/to/MKBT.ASM
bddview --studio-smoke tmp/help-share-ui --demo --help-share
bddview --studio-smoke tmp/background-ui --demo --background
bddview --studio-smoke tmp/subframe-ui --demo --subframes
bddview --studio-smoke tmp/palette-ui --demo --palettes
studio_document_tests tmp/fixture-test path/to/fixture.BDB
studio_game_export_tests tmp/export-test [path/to/fixture.BDB path/to/BGND.ASM]
bddview --studio-export-smoke tmp/runtime-test
bddview --studio-export-smoke tmp/new-runtime-test path/to/fixture.BDB path/to/game-checkout
studio_animation_tests tmp/animation-test [path/to/FOREST2.BDB path/to/game-checkout]
bddview --studio-smoke tmp/forest-ui path/to/FOREST2.BDB --animations
python tools/roundtrip_smoke.py --bddview path/to/bddview --bddtool path/to/bddtool
```

The document test covers zoom anchoring, drag cancellation, independent
history, locks, transparent/flipped picking, camera projection, explicit layer
ownership, save/reopen after repacking, opaque metadata preservation, recovery,
and failed-save behavior. An optional local fixture additionally checks image
IDs, pixels, raw RGB555 palette values and repacked scene positions.

The UI smoke captures Stage, compact-window, Assets and Build & Check views using synthetic
artwork, then drives mouse dragging, keyboard undo/redo and Escape cancellation
through ImGui's input queue. An optional fourth argument loads a real stage
for screenshot inspection instead. Add `--prepare` after that path to prepare
an export in the smoke output folder and capture the review panel; this mode
does not apply to the game checkout. No private game assets are checked in.

The export tests cover tight bounds, centering, parallax at multiple camera
positions, draw order, actor preservation, unsupported inputs, concurrent
source changes, package tampering, apply and backup contents. Build-launcher
tests run tiny local scripts to check successful and failed exit statuses,
working directory and captured logs. They do not run the full game build.
The runtime smoke copies the fixture into a scratch checkout, changes layer
placement/order and artwork, prepares/applies the export, then independently
re-imports it through the legacy runtime parser. It supplies regenerated BMOD
dimensions for that parser; it does not simulate LOAD2 compression or ROM data.
CTest runs this path with generated artwork and an afterimage-list regression
fixture, without requiring private game data.

Animation tests generate their own IMG and assembly fixtures and cover source
sequence decoding, raw/trimmed pixels, palettes, signed frame offsets, camera
projection, playback wrapping, actor-slot preservation, missing/unsupported
sources, independent ownership and exclusion from document saves. The optional
Forest UI smoke clicks pause, next frame and resume through ImGui and captures
a paused animation view. Local Forest2 tests loaded seven images, twenty
sequence steps and three actors; a scratch export/re-import preserved its
placement and ordering at three camera positions. No live game files were
changed, and no new emulator comparison was performed for the animation preview.

Windows Release build, document tests, both local DEDPOOL and NUPOOL fixture
tests, UI screenshots, input smoke, existing round-trip/RGB555 tests, and legacy
module-pick/move-undo/split/palette-compaction smoke commands passed during this
implementation. Linux/macOS verification is configured in CI but was not run
locally. Emulator comparison and the broader specialist-tool migration remain
future work. Game-export tests, build-launcher tests and the runtime re-import
smoke passed against local NUPOOL on September 20, 2026. The live checkout was
only read during verification; full game compilation and MAME were not run.
# Composite PNG export

**File > Export composite PNG** exports the current resolved composition as one tightly cropped PNG. It supports the current selection, current layer, or full visible stage and honors placement palettes, X/Y flips, layer transforms, canvas ordering, and hidden state. Transparent index zero stays transparent; full-stage exports can optionally include the saved background color. The optional companion JSON records the crop origin and every placement's stage and image-relative coordinates so the artwork can be reviewed or reassembled without guessing offsets.
