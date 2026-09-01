# Contributing to midway-bddtool

Thanks for your interest in contributing! This project is a cross-platform
viewer/editor for Midway BDB/BDD background data. This guide covers how to
build, test, and submit changes.

## Before you start

- **Never commit game assets.** Read the *Public Asset Policy* in
  [README.md](README.md) first. No ROMs, `.BDB`/`.BDD`/`.IMG`/`.LOD` stock
  files, MAME state, or extracted art. Keep local working material under the
  ignored `.local-private/`, `tmp/`, or `reference/` paths.
- The project is MIT licensed (see [LICENSE](LICENSE)). By contributing you
  agree your contributions are licensed under the same terms.

## Building

Full per-platform instructions are in [README.md](README.md#building). In short:

```bash
# Linux
sudo apt install libsdl2-dev cmake
cmake -B build && cmake --build build

# macOS
brew install sdl2 cmake
cmake -B build -DSDL2_DIR=$(brew --prefix sdl2)/cmake && cmake --build build

# Windows
powershell -ExecutionPolicy Bypass -File build.ps1
```

The build produces two executables: `bddview` (the SDL2 + Dear ImGui editor)
and `bddtool` (a headless CLI validator/differ).

## Testing

There is no unit-test framework; correctness is guarded by headless smoke
tests that exercise the real load/save/import paths. Run them before opening a
PR — they are exactly what CI runs:

```bash
# Headless CLI round-trip + validation (self-contained, generates its own data)
python3 tools/roundtrip_smoke.py

# Or the individual headless commands
./build/bddview --write-checker-test /tmp/checker
./build/bddview --check-open-mode /tmp/checker.BDB
./build/bddtool validate /tmp/checker.BDB /tmp/checker.BDD
```

CI (`.github/workflows/ci.yml`) builds and smoke-tests on Linux and macOS for
every push to `main` and every pull request. Please make sure it is green.

## Coding conventions

The codebase follows a defensive, mostly C-style approach even though it
compiles as C++17. Match the surrounding code:

- **String formatting:** use `snprintf` (and bounded `strncat` with an explicit
  room check). Never use `strcpy`, `strcat`, `sprintf`, or `gets`.
- **Pixel/buffer math:** compute sizes with `size_t` (e.g. `(size_t)w *
  (size_t)h`), never bare `int`, to avoid overflow on large images.
- **Validate external input:** clamp/reject untrusted data at load time. Image
  dimensions are capped at 4096×4096 and palettes at 256 entries — keep new
  importers consistent with `bdd_core`.
- **Handle allocation failure:** check every `malloc`/`calloc`/`realloc`, free
  partial allocations on the error path, and return a failure code.
- **Project state goes through the storage API.** Do not resize or reallocate
  the `g_*` globals directly. Use the `editor_project_*` functions in
  [platform/Core/editor_project_storage.cpp](platform/Core/editor_project_storage.cpp)
  (`editor_project_reserve_*`, `editor_project_append_*`,
  `editor_project_delete_*`, etc.), which grow capacity safely and refresh the
  global pointers.
- **Undo coverage:** every user-facing mutation should record an undo entry via
  `undo_save` / `undo_save_ex` or one of the delta helpers in
  [platform/undo_manager.cpp](platform/undo_manager.cpp). If you add an editing
  action, add its undo support in the same change.

## Project layout

```text
bddview.cpp                SDL2/ImGui app entry point
platform/bddtool_cli.cpp   Headless CLI (validate/diff) entry point
platform/Core/             Format I/O, project storage, image/palette logic
platform/UI/               ImGui panels, dialogs, overlays, tools, views
platform/undo_manager.*    Undo/redo ring and checkpoint state
platform/libs/             Vendored stb_image (do not modify; own license)
imgui/                     Vendored Dear ImGui (do not modify; own license)
tools/                     Headless smoke scripts and helper utilities
```

`imgui/` and `platform/libs/` are third-party vendored dependencies and keep
their own upstream licenses — avoid local edits so they stay easy to update.

## Submitting a pull request

1. Branch off `main` and keep changes focused on one topic.
2. Build cleanly and run the smoke tests above.
3. Write clear commit messages describing the *why*, not just the *what*.
4. Open the PR against `main`; make sure CI passes.
5. For format/behavior changes, note how you verified them (which smoke
   command, which sample data).
