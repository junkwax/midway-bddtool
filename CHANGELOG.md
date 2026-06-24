# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.26] - 2026-06-24

### Added
- Match Start Placement controls now expose fighter ground Y alongside the
  start camera X/Y, with one-click BGND.ASM patching and stage-config/recipe
  persistence.

### Changed
- Save backups, autosaves, pre-save copies, BGND/BGNDPAL patch backups, and
  outside-object delete backups now go under bddtool's local `backups/`
  folder instead of beside files in the game repo.

## [1.0.25] - 2026-06-23

### Added
- "Hide Unselected" / "Show All Objects" in the canvas right-click menu --
  hides everything except the current selection so it's obvious what a
  new module's bounding box will and won't cover.

### Changed
- "Create Module from Selection" and "Wrap selection in Region" now
  refuse with a toast (instead of silently proceeding) when other
  visible, non-selected objects fall inside the new module's bounds --
  e.g. two ctrl-clicked objects far apart, which unavoidably sweeps in
  anything between them since a module is a plain rectangle. Hide the
  clutter first (Hide Unselected) to signal it's intentional.

## [1.0.24] - 2026-06-23

### Added
- "Fit World Width + Scroll Limits to Content" button (Modules panel,
  next to the manual scroll-limit fields): sets world width and BGND.ASM
  scroll_left/scroll_right to your stage's real min/max content X, so the
  camera can't scroll past where anything is actually placed. There's no
  formula for this in the original game (verified against ARENA/TOWER/
  DEDPOOL -- it's hand-tuned by playtest); use the existing Pan Coverage
  Scanner (MK2 > Check) afterward to confirm slower parallax planes don't
  show gaps across the new range.
- Right-click menu on overlapping/stacked modules now shows a picker
  listing every module under the click point instead of silently acting
  on just one.

### Fixed
- Module lock resolved an object's module via LOAD2's first-fit rule, so
  locking an outer/catch-all module also locked everything visually nested
  inside a smaller, unlocked module overlapping it -- read as "locking
  other assets from being added" rather than locking movement. Lock
  checks now use the same smallest-area-wins resolution as the canvas
  hit-test instead.

## [1.0.23] - 2026-06-23

### Fixed
- Objects vanished entirely in Runtime view as soon as their module got a
  runtime placement ("Set as runtime location" or draft promotion). The
  renderer treated any runtime-bound module as a compiled background plane
  (rendered from real *BLKS block-table data, which only exists for shipped
  stages) and skipped drawing it as a normal sprite -- leaving nothing
  drawn at all for a hand-authored stage with no block table. Now requires
  real block data to exist before taking that path; every module placed
  through this editor's own tools draws as a normal sprite again.
- Status bar's Mouse:/In-game: fields showed near-INT_MAX garbage values
  when the mouse was outside any window.

## [1.0.22] - 2026-06-23

### Added
- Module lock: right-click a module on the canvas, use the "Lock" checkbox
  next to it in the Modules panel, or the LOAD2 Module Summary table's
  [locked]/[unlocked] toggle. Locked modules and their objects can't be
  dragged at all (in, out, or as a group), and are highlighted in orange
  on the canvas. Session-only, like object lock/hide -- not saved with
  the project.

### Fixed
- Multi-object drag never checked the per-object lock flag at all, so a
  locked object could still move if it was part of a multi-selection
  drag started by clicking a different, unlocked object -- the actual
  cause of objects appearing to drift in and out of their module.
- "Set as runtime location" worked once, then failed on the next new
  module with "Could not infer the BGND init block" -- the write-side
  block locator only matched by current module name, the same fragile
  heuristic already fixed once on the read side in 1.0.21.

## [1.0.21] - 2026-06-23

### Added
- New stages get their own per-stage draft BGND.ASM (`<StageName>.BGND.ASM`
  next to the BDB/BDD) instead of requiring edits to the shared, live
  BGND.ASM -- "Create Draft BGND.ASM" generates a starter block from the
  stage's current modules, every Runtime Binding control transparently
  edits the draft, and "Promote to BGND.ASM" merges it into the real file
  (datestamped backup first) when ready. MKSEL.ASM stage-select wiring is
  still a separate manual step.
- Right-click a module rectangle directly in the World View canvas for
  "Set as runtime location" / "Edit runtime placement..." -- previously
  only available from the Modules panel's plane table.
- "Re-bind all not-placed modules" bulk action and a "Unique module names"
  check (Play Readiness Checklist) with a one-click rename fix.

### Fixed
- `bdd_get_stage_module_table`'s cache was never invalidated after writing
  BGND.ASM/its draft, so the Modules panel and Game Preview could keep
  showing pre-edit values for the rest of the session.
- Two more copies of the "snaps to module's own corner instead of staying
  put when there's no real BGND.ASM binding yet" bug (first fixed in
  1.0.19's `bdd_object_runtime_origin`), found in `bdd_object_editor_origin`
  (what World View's Runtime toggle actually renders through) and the
  in-game hover-coordinate code.
- Draft BGND.ASM became undiscoverable after bulk-renaming every module in
  a stage, even though the file and its data were untouched.
- Duplicate module names (e.g. two modules both named the same after manual
  renaming) broke module-drag and BGND.ASM BMOD matching for one of them.
  Module names are now auto-generated stage-prefixed and guaranteed unique.

## [1.0.20] - 2026-06-22

### Fixed
- A module created from a selection could silently receive none of its
  objects if an earlier, world-spanning module (e.g. Simple Mode's
  auto-created catch-all) already enclosed it -- module assignment is
  first-fit by file order, so the earlier module always won. New modules
  from a selection are now inserted before any module that would
  otherwise shadow them. Fixed in all three places this could happen:
  "Create Module from Selection", "Wrap selection in Region", and the
  Modules panel's "+ From Selection"/"+ Cover Stage" buttons.

## [1.0.19] - 2026-06-22

### Added
- "Play Readiness Checklist" tool (MK2 Workflow > Check): BGND.ASM wiring,
  hardware palette budget, world bounds, unused images/palettes, and
  template-default stage names, each with a one-click Fix where a safe
  automated fix exists.
- Modules panel has a "Stage Name" field -- there was previously no way to
  rename a stage's internal BDB header name after creation.

### Fixed
- Modules created from a selection appeared to jump to a different spot
  the moment Runtime Layout / Game Preview was turned on, because an
  unbound module's objects were reported at coordinates relative to the
  module's own corner instead of their actual world position. A module
  now stays put until it's actually bound to a runtime location, and
  binding with the default offset is a no-op instead of causing a second
  jump.
- Module names had no validation despite becoming a literal BGND.ASM
  assembly symbol once bound; invalid names are now rejected with a clear
  message instead of writing broken ASM.
- Game Preview's "Layers:" legend and per-object Layer buttons were a 6th
  independent copy of the old 6-value layer-preset list, missed in
  1.0.18's unification pass.

## [1.0.18] - 2026-06-22

### Fixed
- Five right-click menus and combos (world canvas, Game View, Edit menu,
  Properties panel x2) had their own independent copies of the old 6-value
  layer-preset list, so assigning a layer there never showed the other 12
  real values added in 1.0.17. All five now read from the shared preset
  list.

### Added
- Status bar shows "In-game: (x, y)" next to Mouse: while Runtime Layout
  view is on -- the in-game coordinate under the cursor, accounting for
  each module's runtime screen offset, not just the raw BDB-source position.

## [1.0.17] - 2026-06-22

### Fixed
- BDB palette corruption: the Modules panel "Update Header" button wrote the
  old palette count back unchanged instead of syncing it to the live count,
  and palette names auto-generated from multi-word image names could carry
  an embedded space, which desyncs the BDD palette-table parser for every
  palette after it. Names are now sanitized at creation.
- A brand-new project (0 images, 0 palettes, as every New Project template
  starts) failed to reload after being saved -- `bdd_core_load_bdd` treated
  "zero images" as a load failure instead of a legitimately empty project.
- Layer-byte presets only covered 6 of the 18 real depth-byte values used by
  shipped stages (0x32-0x4E), and their "0.2x".."1.5x" labels implied this
  byte controls scroll speed, which it doesn't -- scroll rate is a
  per-module property set in Runtime Binding, not a per-object one. Presets
  now cover the real range and are labeled by depth tier instead.

### Added
- Modules can get their first runtime placement from the editor: right-click
  an unplaced module in the Runtime Binding plane table for "Set as runtime
  location", or use Apply placement, which now creates the `*BMOD` entry in
  BGND.ASM if one doesn't exist yet instead of failing silently.
- F9 toggles a raw BDD/BDB field debug inspector (Help > Debug Info):
  header string vs. live counts, raw module lines, selected object/image/
  palette fields.
- Palette panel has +/- buttons to add or remove a single color slot
  directly.

## [1.0.16] - 2026-06-21

### Added
- Modules panel can set a level's in-game background color (the autoerase /
  irqskye colour, BGND.ASM <stage>_mod word 1) with a color picker that writes
  RGB555 back to BGND.ASM. This is distinct from View > Background Color, which
  only tints the editor canvas.
- Modules panel can now edit per-module runtime parallax and placement: pick a
  module, set its parallax aggressiveness (0.00 screen-fixed .. 1.00 playfield)
  and its runtime screen X/Y offset, and apply straight to BGND.ASM (with a
  backup). Parallax writes the plane's scroll-table rate and notes that it
  affects every module sharing that baklst plane.
- Runtime palette sync now guards the BGNDPAL.ASM ROM budget: it estimates the
  assembled palette-data size and refuses to write past a soft cap (with an
  override), so cross-stage syncing can no longer silently overrun the reserved
  REVX/MK8MIL ROM region. The sync prompt shows current palette ROM usage.
- "Compact duplicates" button in the palette sync prompt collapses
  content-identical palette blocks and repoints every *PALS table to the
  survivor (with a .pre_bgndpal_compact backup).

### Changed
- Runtime palette sync now reuses an existing palette block whose colours are
  identical instead of appending a duplicate, so re-syncing unchanged stages no
  longer grows BGNDPAL.ASM.
- Block-table background planes honor a per-plane scroll origin (worldtlxN,
  which BGND.ASM helpers such as center_x can seed) instead of assuming every
  plane starts at the stage camera, so pre-seeded/screen-anchored planes track
  correctly in the game preview and runtime layout view.

## [1.0.15] - 2026-06-19

### Added
- Modules can be moved by dragging: with module bounds visible, click and drag an
  empty spot inside a module rectangle to slide the module and every object it
  contains together. Shift+drag still starts a rubber-band selection.
- Modules panel gains a "Runtime Binding (BGND.ASM)" section: shows each module's
  parallax plane, parallax factor, screen offset and draw rank parsed from the
  stage's BGND.ASM, and lets you edit the stage open camera and scroll limits and
  write them back to BGND.ASM (with a timestamped backup).

## [1.0.14] - 2026-06-19

### Added
- "Module from Selection Bounds" command (Object menu) anchors a new LOAD2
  module whose bounds enclose the selected objects' depth/sy footprints, in both
  simple and advanced modes.
- Selected sprite groups can be resized together through a dedicated workflow.

### Changed
- MK2 display-object diagnostics now charge a runtime reserve against the
  on-screen block peak. Non-BDD runtime sprites (the two fighters and stage
  actors such as the Dead Pool hangers) share the getobj pool, so stages that
  fit in the editor but overflow disp_add at scene init are now flagged.

### Fixed
- Runtime palette sync (BGNDPAL.ASM) now writes the exact stored RGB555 words,
  including a true index 0, instead of re-deriving them from the display ARGB
  (which forced index 0 to black).
- Runtime palette sync no longer overwrites another stage's palette block when a
  BDD palette name collides with a label owned by a different stage's `*PALS`
  table; the colliding palette is renamed and written as a fresh block.

## [1.0.13] - 2026-06-16

### Added
- Block Editor can edit the active palette color directly, apply basic
  brightness/contrast adjustment to the block palette, and auto-fit zoom to the
  active block size.
- Palette panel now includes a live preview of the selected palette on related
  stage art, plus local undo/redo controls.

### Changed
- Runtime preview extras and animation imports now respect a preference instead
  of always auto-loading when a stage opens.

## [1.0.12] - 2026-06-10

### Changed
- Derived runtime actors now carry motion and per-plane parallax, derived from
  their BGND.ASM spawn proc:
  - x-velocity from the proc's `movi >v,a0` (oxvel); movers (bats) travel across
    the screen in the GUI preview.
  - the insertion baklst (direct `movi baklstN,b4` or one level through a
    `callr`/`calla`/`jsrp` helper such as `get_bat_obj`/`make_a_mad_tree`)
    selects the actor's parallax factor instead of a fixed 1.0 -- e.g. SPIRAL
    warplite baklst7 = 0.0 (screen-fixed), ARMORY lava baklst3 = 0.656, BRIDGE
    fighters baklst4 = 0.125. `worldtlx`-relative movers are screen-anchored.
  - proc-body scans stop at the `a_*` data table, so a non-spawning "frame
    driver" (e.g. SPIRAL `lil_monk_animator`) is identified and no longer
    emits a stray actor at a spurious position.

### Added
- The runtime preview now spawns vanilla-derived background actors:
  `runtime_actor_autoload_for_stage()` builds them from
  `bdd_stage_runtime_actors()` (BGND.ASM spawn proc -> `a_*` frame sequence ->
  decoded `movi >y:x,a4` positions), importing the frame sprites from
  `MK6MIL.LOD` (which packages `MKBGANI.IMG`). Armory lava, Tomb bats, Spiral
  monks/warplite and Bridge fighters now appear from source data; Forest keeps
  its existing path; stages with no `pid_bani` frame actors (Tower/Battle/Dead
  Pool) are unaffected.
- `--render-png` now also draws the runtime actors (first frame) so the derived
  actors can be reviewed in headless captures.

### Fixed
- The `a_*` sequence parser stops at `ani_*` control opcodes (e.g.
  `.long ani_jump,a_medbat`) so loop markers are not mistaken for frames.

## [1.0.10] - 2026-06-10

### Added
- Vanilla background-animation actor derivation from MK2 source, exposed via
  two diagnostics and a C API:
  - `bddview --mkbgani-info FILE.BDD LABEL...` dumps a sprite's vanilla
    metadata (width, height, x/y offset, palette) from `src/MKBGANI.TBL`.
  - `bddview --stage-actors FILE.BDD` derives a stage's frame-animated
    background actors end to end: each calla `create pid_bani,<proc>` spawn,
    its `a_*` frame-sequence table, the MKBGANI sprite frames, and the static
    spawn coordinates decoded from `movi >y:x,a4` (set_xy_coordinates
    semantics).
  - `bdd_mkbgani_sprite_info()` and `bdd_stage_runtime_actors()`.
  Forest decodes to tree_animator/a_treeroar (20 frames) at exactly
  (328,73)/(624,73)/(913,73); Tower/Battle/Dead Pool correctly yield no
  `pid_bani` frame actors (velocity scroll / colour cycle). Derivation only --
  not yet wired into the runtime actor preview, so rendering is unchanged.

## [1.0.9] - 2026-06-10

### Changed
- The editor runtime-extras floor guide now sources its sprite label, screen-Y,
  and height from the loaded stage's vanilla `<stage>_floor_info` (BGND.ASM)
  instead of the hardcoded `g_*_runtime_guide_defaults` literals. Values match
  the prior constants exactly for Forest/Tower/Battle, so there is no behaviour
  change -- the floor geometry is simply no longer hardcoded.

## [1.0.8] - 2026-06-10

### Added
- Headless `--render-png <BDD> <out.png> [game|layout] [zoom]` capture of a
  stage: `layout` renders the whole runtime layout, `game` renders the in-game
  400x254 view composited at the BGND camera start. Lets stages be reviewed and
  diffed from the CLI without the GUI.

### Changed
- Runtime preview now recognizes every MK2 stage by matching the loaded BDB's
  modules against each BGND.ASM `<stage>_mod` block's baklst `*BMOD` modules,
  replacing the hardcoded four-stage shortlist. All stages (tomb, armory, arena,
  port, bridge, etc.) now render with their derived parallax, draw order, floor,
  and camera instead of a generic fallback.
- Background draw order is derived from `dlists_<stage>` for every recognized
  stage (was forest/battle only); Tower keeps its hand-tuned object
  interleaving.
- The floor descriptor (label, palette, screen-Y, height) is derived from
  `<stage>_floor_info` for every stage.

## [1.0.7] - 2026-06-10

### Changed
- Runtime preview now derives stage parallax offsets, scroll rates, background
  draw order, floor sprite labels, camera start, and scroll limits directly
  from MK2's `BGND.ASM` (`<stage>_mod`, `<stage>_scroll`, `dlists_<stage>`,
  `<stage>_floor_info`) instead of transcribed per-stage constant tables. The
  data is parsed once per stage and cached. Forest, Tower, and Battle runtime
  output is unchanged (verified byte-identical against the previous constants
  via the runtime-preview smoke tests).

## [1.0.6] - 2026-06-09

### Added
- Hover help for MK2 background layer roles and parallax behavior in Game
  Preview.

### Fixed
- Runtime preview floors now project with MK2 floor-code screen-space Y math, so
  the Living Forest floor aligns with the background art at match start.
- The Borders toggle now hides runtime guide rectangles and runtime actor
  boxes/labels while leaving the animated sprites visible.
- Runtime preview smoke tests now assert floor screen-Y projection without
  touching ImGui state in headless mode.
- BGND list parsing now uses portable token scanning so Linux/macOS release
  builds match the Windows build.

## [1.0.5] - 2026-06-09

### Added
- Runtime source autoload now searches the sibling Workplace MK2 source trees,
  including `mk2-readonly/mk2-main/data`, for LOD/IMG art.
- Living Forest preview autoloads its runtime floor and tree-face animation art
  from the MK2 source assets.

### Changed
- Runtime-source sprites are treated as preview/read-only art while their placed
  locations remain movable for stage preview alignment.
- Restored the previous canvas scrollbar behavior.

## [1.0.4] - 2026-06-09

### Added
- Runtime animation autoload on stage open, including inferred IMG frame groups
  for levels without a sidecar.
- Top-menu build status badges for pass/warn/error, palette pressure, and
  detected animation metadata.

### Changed
- Reworked the right-side dock into a Photoshop-style Objects, Images, and
  Palettes workflow with tabbed object properties, repairable dock sizes, and
  cleaner image/palette tables.
- Moved game-preview controls below the stage view so sprites are not covered
  while previewing runtime layout.
- Improved cross-panel selection sync so image, object, property, and palette
  panels follow the same selected sprite/object.

### Fixed
- Canvas scrollbars now stop before docked panels instead of drawing through the
  right rail.
- Group bit-depth reduction dialogs no longer resize the window unexpectedly.

## [1.0.3] - 2026-06-09

### Added
- `LICENSE` (MIT) so the project can accept outside contributions, plus
  `CONTRIBUTING.md` documenting build/test steps and the codebase's conventions.
- Continuous-integration workflow that builds and smoke-tests on Linux and macOS
  for every push to `main` and every pull request.
- Tag-driven releases now publish a Windows x64 zip alongside Linux, macOS, and
  source packages.
- "Reclaim ROM Space" tool (MK2 Workflow > Optimize) that shows the payload
  breakdown and runs a one-click lossless shrink (tight-trim, palette/bit-depth
  compaction, identical-image dedup, unused-art removal) with a before/after
  byte report, plus an opt-in near-duplicate color merge for extra savings.
- "Sprite Wedge Risk" scanner (MK2 Workflow > Check) that flags sprites whose
  transparent edges LOAD2 cannot row-encode cleanly, the cause of the in-game
  missing-triangle-wedge artifact, with focus and tight-trim actions.
- "What to fix & how" guidance in the LOAD2 Doctor: each non-zero diagnosis now
  names the tool or button that resolves it.
- Per-row "how to fix" hints on failing Stage Readiness Gate checks.
- Active-filter highlight and a "Showing N of M images" count in the Images
  panel.

### Changed
- Grouped the MK2 Workflow Optimize tools into labeled sections (Space, Palette,
  Pixels & Color, Dedup & Space, Layout Checks) instead of one flat list.
- Decluttered Images panel cards: size and palette share a line and redundant
  source text moved to the hover tooltip and right-click menu.
- Corrected the README "Project Structure" section to match the current layout.

### Fixed
- Block editor brush now paints a continuous line between mouse samples, so
  fast drag strokes no longer skip pixels and leave stray transparent gaps in
  the edited sprite.
- Pixel and image-index undo/redo now resolve their target by the stable image
  id instead of the slot index, so undoing after reordering, deleting, deduping,
  or compacting images can no longer write into the wrong image.
- TGA importer: reject colour maps over 256 entries (stack buffer overflow),
  bound image dimensions and compute the pixel count with `size_t` (integer
  overflow into a heap overflow), and validate the colour-map depth.
- Undo/redo: `redo_restore` now builds the restored entry before mutating the
  ring, so an allocation failure can no longer silently drop the oldest undo.
- Guarded the object-drag grid snap against a zero grid spacing (divide-by-zero).
- Removed a dead duplicate error assignment in `bdd_core_load_stage` and
  bounds-guarded the sprite-sheet `.png` extension append.
- Windows build helpers now pass `VERSION` into CMake so cached build dirs cannot
  keep stale executable resource metadata.

## [1.0.2] - 2026-06-09

### Changed
- Release builds now derive the app version from the pushed `v*` tag, keeping
  the window title, About dialog, and Windows resource metadata in sync.
- GitHub releases now include an explicit source archive and generated release
  notes instead of the old fixed "Initial release" text.

## [1.0.1] - 2026-06-03

### Fixed
- Windows builds now register the bundled app icon as the SDL window icon,
  Win32 class icon, executable resource icon, and AppUserModelID identity so the
  taskbar no longer falls back to the generic paper icon.
- The welcome screen's New Project actions now use the shared unsaved-action
  flow and hide the welcome window while the modal is open, so clicking
  "New Project..." actually opens the project dialog.

## [1.0.0] - 2026-06-02

### Added
- Tag-driven GitHub release workflow that builds Linux and macOS zip packages.
- Version string in the window title bar (`BDD Viewer v1.0.0`) in place of the
  previous `?` placeholder.
- "Cleanup & Tools" collapsible section and an "Assets" divider in the Images
  panel so the panel leads with its filters and asset grid.
- Module-driven parallax for the game preview: each LOAD2 module acts as a
  parallax plane whose scroll rate is derived from the dominant layer of its
  objects, so authored modules drive parallax on any stage (authentic BATTLE
  modules keep their known runtime rates).
- `MAME_HOME` environment-variable fallback for the MAME preview tool.

### Changed
- Unified the blue accent color across interactive widgets (checkmarks, sliders,
  grips, selection, nav-highlight) to match the toolbar.
- Locked the menu bar, toolbar, and document/info strip flush at the top with no
  gaps; the canvas grid now starts immediately below them.
- Top-level menus now require a click to open instead of opening on hover.
- Game Preview / Runtime Layout controls panel moved from the top-left corner to
  bottom-center, next to the Play/Bounce transport.
- Collapsing a docked right-rail panel now lets the panels below it slide up.
- Toolbar buttons are vertically centered within the strip.
- Trimmed the welcome-screen quick-start hint so it fits alongside the
  "Don't show on startup" checkbox.

### Fixed
- Recent-file rows no longer overlap their "time ago" label (paths are
  left-aligned and truncated to leave room).
- Replaced characters the default font cannot render (which showed as `?`):
  welcome-screen arrows, the off-screen-warning en-dash, and the object-list
  lock/hide column headers; the status-bar health indicator is now a drawn dot.
- The zoom (`1x`) readout no longer overflows the toolbar into a scrollbar or
  overlaps the buttons on a narrow window.
- Canvas scrollbars are drawn on the background layer and yield to overlapping
  panels, so they no longer paint over windows.
- "Don't show on startup" checkbox is no longer clipped at the window edge.

### Removed
- Hardcoded `C:\MAME` default paths in the MAME preview tool (now empty by
  default, configurable in-app or via `MAME_HOME`).
