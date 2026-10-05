# Asset workflow decisions and migration

The goal is a smaller, dependable editing workflow, not feature-for-feature
reproduction of the old tool collection. Keep useful operations, combine their
entry points, and make routine file maintenance automatic. A legacy button is
not evidence that the new UI needs another tool.

## What belongs where

| Decision | Operations | Reason / current implementation |
| --- | --- | --- |
| Keep in Assets | File/folder import, artwork selection, block painting, palette editing, image export | Everyday authoring; operate on one document and share its undo history |
| Keep in Stage | Placement, alignment, flips, layer assignment, camera/start position | Direct controls in the canvas/inspector; no separate camera/start/layer-role dashboards |
| Combine in Optimize | Transparent trim, duplicate/mirror search, palette/BPP proposals, repeat/tile and subframe savings | One preview/apply workflow with visual and cost checks; don't migrate each legacy assistant as a panel |
| Combine in Build & Check | Readiness, LOAD2 constraints, X-order, module ownership, stale output, palette pressure, pan coverage, visual diagnostics | One issues list and one export/build workflow; retain useful checks rather than the old dashboards |
| Automatic on Save | Header counts, companion files, metadata, lossless source-layout maintenance, backups, serialization verification | Already automatic except the new readback verification added in this pass; no “sync counts” or “save all companions” prerequisite |
| Automatic during game export | Runtime X-order preparation, bounds/table generation and equivalence checks | Belongs to the export copy. Save must not silently change overlap/drawing order |
| Keep deliberate and reviewable | Palette/index remapping, unused-art removal, recoloring, crop/split/merge, runtime assembly changes | Can change sharing, external references, animation or game drawing even when the static editor picture matches |
| Retire as separate new-UI concepts | “Run Safe Fixes,” guessed layer assignment, delete-outside shortcuts, redundant repair/readiness/preview panels, application-wide Simple/Advanced mode | Replace with explicit editing, useful diagnostics and automatic bookkeeping; do not copy the old workflow scaffolding |
| Defer to optional specialist workflows | Stage-specific builders, actor/FX recipes, ROM reallocation, LOD workspace import | Useful for particular projects; not required to arrange or save a stage |

“Retire” here means no corresponding standalone tool in the rebuilt interface.
It does not delete the old implementation or remove access through `--legacy-ui`.
Rows describing consolidation are design decisions; they are not claims that
every listed legacy check has already been extracted.

### Consolidated authoring checks — October 4, 2026

Build & Check now puts a bounded, searchable findings list ahead of game
integration. Filter by severity or References, Artwork/palettes, LOAD2 and
Layers. Errors appear first. Each finding explains the next step; applicable
findings open the artwork, inspect its specific palette variant, or select and
frame the placement on Stage. Layer findings select the layer. Hidden content
stays hidden, with a notice when located. Copy report includes all findings,
even when the list is filtered.

The document-owned checker covers missing image/palette/layer references,
duplicate or invalid image IDs, dimensions/pixel storage, palette counts and
pixel-index ranges for both default and placed palette variants. It includes
hidden and unplaced artwork. Image-level findings are emitted once per image;
palette-index findings once per image/palette pair, instead of once per repeat.
Independent placement errors remain visible even when its image is missing.

LOAD2 checks cover width alignment, MK2's block scan width, image-header/palette/
module caps, an explicitly uncompressed block-size estimate, and a warning
when static placements use more than 35 background palettes. Width faults on
placed images are errors; unplaced width faults are warnings. These are static
diagnostics, not new Save blockers or proof of runtime failure/success. Compression
and runtime allocation require build/game checks. “No current authoring issues”
does not mean animation, visibility or packed output was tested.

The file-info badge and findings list share results, refreshed after edits and
undo/redo. Checking or following a finding does not repair, save, or modify the
artwork. Counts/backups/readback remain automatic on Save; runtime X-order
preparation remains part of the isolated game-export copy.

### Camera and resource checks — October 5, 2026

**Build & Check → Scan camera range** runs a cancellable background scan. Its
warnings join the same findings list under **Camera / resources**, with **View
camera** links to the sampled position. No new tool window or repair operation
is involved. The report includes the range, sampling step, reserve, peak counts
and limits of the analysis.

The initial horizontal range runs from the authored start to world width minus
400 (or the start if larger); Y defaults to the authored start. These are
editable assumptions, not verified gameplay limits. The scan samples a 400×254
viewport, includes both range endpoints even when the step does not divide the
range, and caps work at 2,048 samples / 8,192 placements. The default step is 16
pixels; increase it or narrow the range for large two-dimensional scans.

It counts intersecting static placement bounds and distinct visible palettes,
including editor-hidden artwork because those placements are still saved.
Object pressure includes an adjustable reserve (56 by default) for fighters,
effects and stage actors against the shared 358-object pool. It warns above
that capacity and about low remaining headroom. Palette pressure is flagged
above 35 visible background palettes; allocation lifetime and animation
palettes remain outside this estimate.

Coverage finds horizontal spans with **no static artwork bounds anywhere in
the viewport's height**. This catches potentially unintended blank columns,
including an entirely empty viewport. Intentional gaps and runtime overlays
can explain them. It does not check transparent pixels, vertical holes,
occlusion, or prove that every camera between samples is safe. Missing source
references are omitted and unmapped layers use authored transforms; unresolved
mapping and custom stage profiles produce an explicit qualification.

Document or scan-option changes mark the result stale and remove its findings
from current counts/reports. Undo can restore a matching result. Partial or
cancelled results are not used. Opening a camera only changes the editor view;
hidden artwork remains hidden. A scan never saves, modifies pixels, prepares an
export, runs a game build, or changes a checkout.

Remaining consolidation work includes generated-output freshness and deeper
pixel coverage in this list. Existing Optimize/export tools still own their
analyses; their checks have not all been migrated.

### Why a generic cleanup-on-save is not appropriate

The old [readiness gate](../platform/UI/tools/mk2_stage_readiness_gate.cpp)
combines four operations under **Run Safe Fixes**: include unassigned artwork
in modules, fit bounds, sort objects, and sync header counts. Only the counts
are unambiguously bookkeeping. Layer ownership and draw order require more
context. Similar outside-module controls appear in
[authoring tools](../platform/UI/tools/mk2_authoring_tools.cpp) and the
[LOAD2 doctor](../platform/UI/tools/mk2_load2_doctor_tool.cpp).

Likewise, [batch image cleanup](../platform/UI/tools/mk2_image_cleanup_tools.cpp)
combines trimming and palette remapping. Those belong in an optimization preview,
not every Save. SPIRAL's validation already demonstrated runtime differences
from palette changes despite matching static art. “Unused” in a BDB does not
establish that assembly or an animation never references an asset.

The proposed standalone palette-cleanup migration was stopped in this pass.
Future palette removal/merging should use Optimize's reference review and
before/after comparison, not add another maintenance panel or run automatically.

### Save defaults

Save already rebuilds header counts, writes BDB/BDD/layout/metadata together,
preserves unknown metadata and compatible raw RGB555 words, and prepares backups
plus interrupted-save recovery. It repacks source rectangles when necessary to
retain explicit layer ownership and authored stage positions; custom stage
profiles retain their protected source-coordinate rules. It refuses to guess
ownership for unassigned artwork that would change during repacking.

Save now also checks image IDs/dimensions/pixel storage and **reads back the
temporary BDB/BDD before replacing existing files**. It compares image order,
IDs, flags, dimensions, pixels, palette names/counts/serialized words, placement
records and module bounds. Failure leaves the existing targets and save point
intact. Recovery saves use the same check. This is file-integrity verification;
it does not simulate LOAD2 or prove in-game rendering.

No automatic palette merging/deletion, reindexing, recoloring, cropping, width
padding, layer guessing, or game-source patching was added to Save. These choices
must not be hidden under a general “clean up” switch.

## Restored in the new workspace — October 4, 2026

| Workflow | Entry point | Behavior |
| --- | --- | --- |
| PNG/TGA/BMP import | File or Assets → Import PNG / IMG; Assets toolbar; file drop | Exact RGB555 conversion, alpha below 128 becomes index 0; at most 255 opaque colors |
| Folder import | Assets → Import → PNG / TGA / BMP folder; File/Assets menus | Review selected files, see image/palette counts, optionally reuse exact palettes; one undoable batch |
| IMG artwork import | Same import controls | Filter/select up to 128 images per batch; decode raw/trimmed images, preserve palette words/order and animation-point metadata; one undo action |
| Block pixel editing | Assets → Edit block; double-click/right-click a thumbnail; placement inspector | Continuous brush, transparent eraser, right-click eyedropper, zoom, scrollbars, pixel grid and X/Y bitmap flips |
| Stroke history | Block editor → Undo stroke / Redo stroke | Local draft history; Apply records one document edit; Cancel leaves the document unchanged |
| Palette editing | Assets → Edit palette; thumbnail context menu; placement inspector | Choose palette, rename, edit RGB555 colors and adjust count; shrinking refuses used indices |
| Separate an artwork palette | Palette editor → Copy palette for this artwork | Copies the saved palette and rebinds this image's uses of that palette; other palette variants remain; undoable independently of color Apply |
| Export an image | Assets → Export PNG; thumbnail context menu | Exports the selected image with its active/default palette and transparent index 0 |

Palette edits affect all uses of that palette. Pixel edits affect all placements
of that image, including uses with other palettes. Painting refuses indices
outside any palette used by the image. These are authored-art edits; they do
not establish game-runtime equivalence or override export validation.

IMG import adds static artwork to Assets without placing it or creating a
runtime animation. Existing Forest animation overlays and the Optimize IMG
analysis picker remain independent. External/default palettes missing from an
IMG are listed as unavailable; unsupported selections fail without changing the
document. The shared decoder retains its two-million-pixel batch limit and
1024-pixel source-dimension limit. Narrow IMG images retain its minimum decoded
width of three pixels; Build & Check still reports LOAD2 width constraints.

The modal editors keep their drafts local. Save/autorecovery serialize only
applied edits; document undo restores the full bank and palette assignments.
Import never writes to its source PNG/IMG or to external game assembly.

## Remaining candidates, subject to the decisions above

| Legacy capability | Current route / next work |
| --- | --- |
| IMG folders and LOD-driven imports | Defer; extend the existing import flow only when source selection and dependencies are clear |
| Palette merging, delete-unused, rebuild/reduction and smart grouping | Combine useful algorithms in Optimize after reference checks; retire overlapping assistants |
| Palette brightness/contrast, tone matching and blending | Candidates for controls inside the palette editor with live preview, not additional tool windows |
| Palette export/import and palette-animation tooling | Separate palette exchange from runtime animation; add only concrete needed workflows |
| Specialist block/subframe operations | Keep useful crop/split/merge actions in the block editor with placement/metadata preservation |
| Sprite-sheet/composite/TGA export | Consolidate under Export; avoid a menu/tool for each format |

Folder import scans one directory, up to 2,048 raster filenames, and reviews up
to 128 selected images / 16 million decoded pixels at a time. Review retains
decoded artwork, so Import applies the exact reviewed data rather than silently
rereading changed files. Any selected decode/color/limit failure blocks the whole
batch. Selection or palette-policy changes invalidate review; a changed document
requires a new review. Nothing is placed or saved automatically. Exact palette
reuse is optional for the incoming artwork and does not merge existing palettes.

The new Optimize workspace already provides verified subdivision/reuse and
repeat/mirror proposals. It is not a substitute for every manual legacy block
operation. Launch `bddview --legacy-ui [file.BDB]` for capabilities still listed
above; no legacy tool was removed by this migration.

## Verification

The document and animation tests cover atomic IMG batch failure, compressed
pixels, palette sharing, unique IDs, metadata, palette-copy bindings, document
isolation, invalid color rejection, undo/redo, and exact save/reopen behavior.

```text
ctest --test-dir BUILD -C Release -R "^studio_(document|animation)$" --output-on-failure
bddview --studio-smoke NEW_OUTPUT --demo --asset-tools [FIXTURE.IMG]
bddview --studio-smoke NEW_OUTPUT --demo --batch-import
bddview --studio-smoke NEW_OUTPUT --demo --checks
bddview --studio-smoke NEW_OUTPUT --demo --camera-checks
```

The UI smoke paints a continuous stroke through ImGui events, applies it,
undoes it, applies a palette draft, imports selected IMG artwork when supplied,
imports a generated PNG, and checks undo. Captures include the Assets page,
block editor, palette editor and IMG picker. It uses scratch assets only.

October 4 Windows Release verification passed: document, animation,
animation-optimizer and game-export suites (4/4), the asset-tools input smoke,
and the existing canvas drag/undo/redo/Escape smoke. Screens were inspected at
1000×720. Local build: `tmp/studio-build/Integration/bddview.exe`.

The subsequent folder-import/save pass also passes those four suites and both
existing UI smokes. Its batch smoke passes at 900×640: review, exact palette
reuse, one-step undo/redo, failed-file rejection, Cancel and verified Save.
Save/reopen and repacking checks also pass on a frozen local SPIRAL pair.
Malformed serialized palette data and undersized pixel buffers are rejected
before replacing originals. No live game checkout or ROM installation changed.

The consolidated-checks pass passes the document and game-export suites plus
the checks, canvas, navigation and asset-tools UI smokes. At 900×640 the checks
smoke exercises severity/category/search filters, placement framing, artwork
navigation, report copying and cache refresh after undo. Core tests exercise
repeated and hidden placements, alternate palettes, unplaced artwork, malformed
storage, duplicate IDs, independent reference errors and the LOAD2 header cap.

October 5 camera-scan verification: Windows Release build; camera checks,
document, game-export and synthetic runtime-export suites (4/4); camera/checks,
canvas, navigation and asset-tools UI smokes. The 900×640 camera smoke exercises
scan launch, camera navigation, stale-result removal after edits/options,
restoration after undo, and copying the report. Core tests cover viewport edge
contact, interior gaps, endpoint sampling on both axes, fractional/negative
parallax, source offsets, hidden placements, palette uniqueness, reserves,
invalid/unresolved input, cancellation and bounded workloads. No game build or
emulator was run for these estimates.

## Help and stage sharing — October 5, 2026

The new Help menu restores **About bddtool / build information**, **GitHub wiki**,
**Stage catalog**, and **Share a stage**. About displays the application version,
compile timestamp, architecture, compiler, SDL runtime and ImGui versions, with
a copyable build summary. Editing shortcuts remain in Help. Sharing is also
available through **File → Share stage**.

Share Stage prepares a new local folder from a document snapshot, including
current applied edits without changing the open document's save point, path,
undo history or source files. Set a bundle/wiki name (letters, digits and
underscores), author, description, sources, use/license, and game testing notes.
The bundle name names the files/page; it does not rename internal game stage or
module identifiers. Author and licensing context are required; no redistribution
permission or emulator testing is assumed by default.

The ZIP contains BDB, BDD, `.bddstudio`, `.BDD.meta`, an overview PNG, a 400×254
start-camera PNG, every image as a prop PNG, and one wiki Markdown page. Previews
include saved hidden placements and all BDD artwork is included in the archive,
including unplaced images. The page records authoring findings and layer data.
Game assembly, runtime IMG animations, emulator output and a scrolling movie
are not bundled. It directs recipients through Build & Check rather than
inventing stage-specific assembly bindings. The legacy animated-media exporter
remains available separately.

Review the folder/page, then choose **Open GitHub submission draft**. Attach the
ZIP and submit it in the browser. The app does not authenticate, post an issue,
upload files, approve a stage, or push the wiki. The existing repository workflow
publishes accepted submissions after a maintainer's `stage-approved` label.
Document or credit changes disable the draft action until a fresh bundle is
built. Existing output folders are refused; failed work remains local and is
never offered as a ready submission.

The wiki publisher now copies and rewrites links to both editor companions and
pins them with binary Git attributes, alongside BDB/BDD, so checkout line-ending
conversion does not corrupt their bytes. Older wiki clones receive these new
attributes too. Remote wiki availability and actual publication were not tested;
no issue or wiki content was posted during development.

Verification: Release build, `studio_share` snapshot/savepoint/undo/PNG tests,
ZIP CRC/content checks, two offline publisher tests, and the 900×640 Help/Share
UI smoke (About/menu navigation, bundle creation, page copy and stale-credit
handling). Fixtures use synthetic artwork. Commands:

```text
ctest --test-dir BUILD -C Release -R "^studio_share$" --output-on-failure
python tests/test_publish_stage_to_wiki.py
bddview --studio-smoke NEW_OUTPUT --demo --help-share
```

The overview is bounded to 2048×1024, the source artwork to 16 million pixels,
and each preview to 128 million compositing operations. The camera image stays
400×254. Structural reference/pixel errors must be resolved before sharing;
runtime/LOAD2 findings remain visible in the page rather than being described
as passed. Checks and screenshots are authoring evidence, not game verification.
