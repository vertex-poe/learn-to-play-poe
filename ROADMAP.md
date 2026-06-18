<!-- ROADMAP.md (markdown) -->

# Roadmap

## Phase 0 — Toolchain Bootstrap *(complete)*

> Goal: `just build` produces a window; `just test` runs green; CI passes.

- [x] `CMakeLists.txt` — root CMake, Qt6 find, CTest enabled
- [x] `CMakePresets.json` — `debug` / `release` / `windows-mingw` presets
- [x] `vcpkg.json` — manifest mode; `tomlplusplus` dep wired up
- [x] `Justfile` — `configure`, `build`, `test`, `clean`, `run` recipes
- [x] `src/main.cpp` — minimal `QMainWindow` smoke app
- [x] `src/CMakeLists.txt` — `qt_add_executable` target
- [x] `tests/test_placeholder.cpp` — QTest hello-world
- [x] `tests/CMakeLists.txt` — `add_qt_test` helper, CTest registered
- [x] `.vscode/tasks.json` — configure / build / test tasks pointing at cmake
- [x] CI — update GitHub Actions to install Qt6 + vcpkg and run `cmake --preset`

---

## Phase 1 — Shell Application *(complete)*

### Qt Project Setup
- [ ] Set Windows manifest and version resource *(Phase 5)*
- [x] Embed application icon (`assets/logo/vertex-icon.png`) — runtime app/tray icon via Qt resources
- [ ] `windeployqt` CMake install step for Windows distribution *(Phase 5)*

### Window & Tray
- [x] `QApplication` + `QMainWindow` main window (`src/MainWindow.h/.cpp`)
- [x] `QSystemTrayIcon` with icon from assets
- [x] Tray context menu: Open, Settings (disabled), Exit
- [x] Left-click tray icon → show/raise main window
- [x] Hide to tray on window close (override `closeEvent`)
- [x] Start minimized setting (launch hidden, tray visible) *(Phase 2 — needs config)*
- [x] Restore window to foreground from tray

### Activity Log
- [x] Scrolling timestamped log view (`QPlainTextEdit`) — auto-scroll
- [x] Log entry on app start
- [ ] Log entry when game window detected / lost *(Phase 3 — needs tracker)*

---

## Phase 2 — Config Persistence *(complete)*

- [x] Load/save config from TOML file via `tomlplusplus` (port `config.rs`)
- [x] Settings dialog / panel with live save on change
  - [x] Toggle: Auto-detect game install directory
  - [x] Text field: Install directory (disabled when auto-detect is on)
  - [x] Text field: Windows executable name (`PathOfExile.exe`)
  - [x] Text field: Linux executable name (`PathOfExile`)
  - [x] Toggle: Start minimized
  - [x] Toggle: Minimize to tray on close
  - [x] Toggle: Auto start on boot *(stub — disabled in UI, coming soon)*
- [x] Fall back to CWD for dev (`cmake --build` workflow)
- [x] Write default config on first launch if absent

---

## Phase 3 — Window Tracker

- [ ] Abstract `WindowTracker` interface (port `tracker.rs`)
- [ ] Windows implementation: `FindWindowA` + `GetWindowRect` + `QueryFullProcessImageNameW`
- [ ] Linux implementation: X11 window tree walk by title
- [ ] Auto-detect game install dir from process image path (Windows)
- [ ] Poll game window every 1 s; update log on state change
- [ ] Log entry when install directory is auto-detected

---

## Phase 4 — Game Overlay

> Qt removes the IPC/child-process complexity from the Rust prototype — both windows live in the same process.

- [ ] Overlay as a second `QWidget` in the same process (no child process)
- [ ] Window flags: `FramelessWindowHint | WindowStaysOnTopHint | Tool`
- [ ] Transparent background: `WA_TranslucentBackground`
- [ ] Track game window rect; reposition/resize overlay each poll cycle
- [ ] Fall back to large default rect when game is not found
- [ ] Per-region hit-testing via `QWidget::setMask(QRegion)`:
  - Background / empty areas → click-through to game
  - UI widget areas → interactive, capture mouse
  - Update mask on overlay layout change
- [ ] Show/hide overlay with game window (show when found + overlay enabled)
- [ ] Config changes propagate directly (shared object, no IPC)

---

## Phase 5 — Polish & Packaging

- [ ] Cross-monitor DPI — verify Qt sets `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`
- [ ] Port DPI drag stability test to Qt integration test or manual checklist
- [ ] `linuxdeployqt` / AppImage for Steam Deck
- [ ] `macdeployqt` for macOS `.app` bundle
- [ ] Installer (Inno Setup for Windows, or Qt Installer Framework)

---

## Future

- [ ] Auto start on boot (Windows registry `HKCU\…\Run`; Linux `.desktop` autostart)
- [ ] Companion mode: web API only, no overlay, no PoE install required
- [ ] macOS overlay (`NSWindow` level + `ignoresMouseEvents`; needs PoE Mac client testing)
- [ ] Game overlay interactive content beyond proof-of-concept text
