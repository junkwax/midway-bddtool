# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
