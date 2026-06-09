# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
