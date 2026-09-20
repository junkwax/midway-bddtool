# BDD Studio — first implementation

The application now opens in a new stage placement workspace. The existing
format code is shared by Studio, the original editor, and `bddtool`. The
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
  PNG, TGA, and BMP, using transparency below alpha 128 and exact RGB555 colors.
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

## Camera and runtime interpretation

On opening an unmodified stage, Studio reads runtime plane offsets, camera
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

This is an authoring preview. Runtime actors, animated palette effects,
game-specific floor deformation, and compiled ROM output are not reproduced
or emulator-verified. Inferred runtime bindings do not establish complete
game fidelity. Stages opened without runtime source need their layer positions
arranged manually in this first implementation.

## Save and recovery

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
layer transforms; use it for its specialist operations, then review Studio's
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

1. Choose the game checkout. Studio detects it when opening `data/STAGE.BDB`
   beneath a folder containing `src/BGND.ASM`.
2. Click **Prepare game export**. This takes a snapshot of the current live
   document, including unsaved edits, and creates a new export package. It
   does not modify the game checkout or mark the document saved.
3. Review the report. **Copy export folder** locates `REVIEW.txt`, `BGND.diff`,
   the BDB/BDD pair, metadata, `.bddstudio`, and the patched `src/BGND.ASM`.
4. Click **Apply reviewed export**. Studio checks that the document revision,
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

Packages live under the application's preferences directory in `exports/`.
Each has a unique directory and keeps its original backups. `APPLIED.txt`
records a completed apply; `APPLYING.txt` identifies an interrupted transaction
and lists the originals to restore. Failed replacement attempts roll back.
The multi-file apply is not atomic across a machine crash. Do not remove an
unfinished journal until its listed files have been recovered.
A checkout-level `.bddstudio-applying/RECOVERY.txt` points to the package
when an apply is interrupted. Studio refuses further exports to that checkout
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
`build.py` already performs complete BLKS/BMOD promotion. Studio does not
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

History retains up to 64 edits. Image imports currently require at most 255
opaque RGB555 colors; palette reduction, pixel editing, IMG/LOD workflows,
full LOAD2 diagnostics and ROM deployment remain in the specialist editor.
**Build & Check** currently checks missing references, layer assignment,
runtime binding availability, and MK2 width constraints. It is not yet the full
complete ROM packaging pipeline.

## Verification

```text
ctest --test-dir build -C Release --output-on-failure
bddview --studio-smoke tmp/studio-ui
studio_document_tests tmp/fixture-test path/to/fixture.BDB
studio_game_export_tests tmp/export-test [path/to/fixture.BDB path/to/BGND.ASM]
bddview --studio-export-smoke tmp/runtime-test
bddview --studio-export-smoke tmp/new-runtime-test path/to/fixture.BDB path/to/game-checkout
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

Windows Release build, document tests, both local DEDPOOL and NUPOOL fixture
tests, UI screenshots, input smoke, existing round-trip/RGB555 tests, and legacy
module-pick/move-undo/split/palette-compaction smoke commands passed during this
implementation. Linux/macOS verification is configured in CI but was not run
locally. Emulator comparison and the broader specialist-tool migration remain
future work. Game-export tests, build-launcher tests and the runtime re-import
smoke passed against local NUPOOL on September 20, 2026. The live checkout was
only read during verification; full game compilation and MAME were not run.
