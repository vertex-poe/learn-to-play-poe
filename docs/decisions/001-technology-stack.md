# ADR-001: Technology Stack Selection

**Status**: Decided  
**Date**: 2026-06-18  
**Deciders**: MovingCairn

---

## Context

This app is a desktop companion for Path of Exile 1 with two distinct runtime surfaces:

1. **Main app** — settings panel, activity log, system tray integration, game-process detection
2. **Game overlay** — a transparent, always-on-top window that renders UI elements (text, inputs, buttons) over the game while the game remains playable underneath

### Platform targets

| Platform | Architecture |
|---|---|
| Windows 11 desktop | x86_64 |
| Steam Deck (desktop mode, SteamOS/KDE) | x86_64 (AMD Zen 2 — not ARM) |
| macOS (if PoE runs) | ARM64 / x86_64 |

The app may also run as a **companion-only mode** on any platform — no overlay, no game filesystem access — consuming only PoE web APIs (character data, stash, etc.).

### Distribution requirements

- Installer with bundled DLLs is acceptable (no single-file requirement)
- No requirement to install a runtime (interpreter) separately
- Startup speed matters; overlay rendering must be smooth (~60fps)
- UI does not need to match the OS native look — custom styling is fine

---

## What was tried first: Rust + egui/eframe + egui_overlay

The prototype was built in Rust using:

```toml
eframe        = "0.29.0"       # egui native shell (winit + wgpu/glow)
egui          = "0.29"         # immediate-mode GUI
egui_overlay  = "0.9.0"       # overlay (GLFW + three-d — different windowing stack)
tray-icon     = "0.24.1"       # system tray
windows       = "0.52"         # raw Win32 APIs
x11rb         = "0.13"         # X11 window tracking (Linux)
```

### Pain encountered

**Structural mismatch — two incompatible windowing stacks in one app:**  
`eframe` uses **winit** as its windowing backend. `egui_overlay` uses **GLFW + three-d**. These two backends each require ownership of the main thread's event loop and cannot coexist in one process. This forced the overlay to be a separate binary launched as a child process, with JSON config piped over stdin as IPC.

**Basic window lifecycle had no first-class API:**  
egui/winit has no proper "hide to tray" concept. The workaround was `ViewportCommand::Visible` sent conditionally via a diff (`was_visible: Option<bool>`), with visibility state duplicated between an `Arc<Mutex<AppState>>` for the tray thread and `MainApp` fields for the render thread — copied back and forth every frame. This created a lost-update race where tray click → show was unreliable.

**System tray required a background thread with manual repaint pokes:**  
`tray-icon` events arrive on a background thread. Driving egui from that thread requires calling `ctx.request_repaint()` and re-reading shared state on the next frame. There is no event-post mechanism. Exit was handled with `std::process::exit(0)` from the tray thread, which skipped `Drop` and orphaned the overlay child process.

**DPI ping-pong on cross-monitor drag:**  
winit internally calls `SetProcessDpiAwarenessContext` at startup. The wrong context was being set for the use case (per-monitor vs system aware). Required manually calling the Win32 API before winit's `EventLoop::new()` — a timing-sensitive, Windows-only hack.

**`WS_EX_TOOLWINDOW` disabled:**  
Setting `.with_taskbar(false)` adds `WS_EX_TOOLWINDOW` to the window, which is the correct way to suppress taskbar entries. Disabled because it triggered an infinite resize loop on cross-monitor drag — a known winit bug.

**Overlay was click-through only (not interactive):**  
`egui_overlay` applies `WS_EX_TRANSPARENT` to the entire window (all input passes through). This was not a deliberate design choice — it was the only mode that worked for rendering on top of a game. It precludes interactive overlay UI (buttons, text inputs).

### Root cause assessment

~60% of the pain was stack mismatch: egui/eframe is an excellent immediate-mode renderer but was designed for tool/dev-tool windows, not apps with tray, overlay, and OS-level window lifecycle. ~25% is genuinely hard on any stack (cross-monitor DPI, overlay z-order, foreground-steal restrictions). ~15% was fixable code bugs in the implementation.

---

## The overlay interaction model (clarified)

The "click-through only" mode was a workaround, not a requirement. The actual desired behavior is:

- **Transparent background areas**: visually transparent, mouse clicks pass through to the game
- **UI element areas** (buttons, inputs): opaque, interactive — captures mouse clicks normally

This is **per-region hit-testing**, not whole-window click-through. Windows supports this via `WM_NCHITTEST` (return `HTTRANSPARENT` for background regions, `HTCLIENT` for UI regions) or `SetWindowRgn`. This is how Discord overlay, Steam overlay, etc. work on windowed games.

Requirement: the game must be in windowed or borderless-windowed mode. True exclusive fullscreen locks the swap chain and cannot be overlaid by an external window regardless of technology.

---

## Options evaluated

### Option A: Rust + egui (stay, fix bugs)

Fix the lost-update race, use eframe multi-viewport for the overlay (same process, same winit event loop), set overlay Win32 styles via `raw-window-handle` after creation.

**Pros**: No rewrite, preserves existing Rust code.  
**Cons**: eframe multi-viewport + overlay Win32 styles is undocumented territory. Tray will always be awkward (no native integration). winit bugs (DPI, toolwindow resize loop) are upstream issues not under project control. egui is the wrong abstraction layer for the app-shell concerns.

### Option B: Rust + cxx-qt

Use Qt via `cxx-qt` (maintained by KDAB) as the UI layer with Rust for logic.

**Pros**: Rust for business logic, Qt for UI.  
**Cons**: Requires CMake + Cargo + cxx-qt codegen — three build systems. Thin AI training data. `cxx-qt` still maturing (API churn). FFI complexity adds friction without clear benefit for a prototype. The UI/logic split isn't valuable here since the "logic" is thin.

### Option C: Python + PySide6

PySide6 is Qt for Python (maintained by The Qt Company). All Qt capabilities, friendly syntax.

**Pros**: Fastest iteration for vibe coding. Best AI training data. All Qt features first-class.  
**Cons**: Requires Python runtime. PyInstaller/Nuitka packaging is slow to start, large, and fragile (antivirus false positives, slow cold start). User has tried Python packaging and found it unacceptable.

### Option D: C# + Avalonia

Avalonia is a cross-platform .NET UI framework with a Skia-based renderer (not native OS widgets). `dotnet publish --self-contained true -p:PublishSingleFile=true` produces a single executable with embedded runtime (~80MB).

**Pros**: Self-contained publish, C# friendly for vibe coding, retained-mode (real window lifecycle), system tray in Avalonia 11+, transparent frameless windows supported.  
**Cons**: Overlay hit-testing requires Win32 interop via P/Invoke (more work than Qt's `setMask()`). Smaller ecosystem than Qt. NativeAOT publish is experimental.

### Option E: C++ + Qt — Selected

Qt/C++ with `windeployqt` (Windows), AppImage/`linuxdeployqt` (Linux), `macdeployqt` (macOS). Installer bundles DLLs.

See Decision section.

---

## C++ + Qt6 vs Go + Fyne — detailed comparison

Go was a credible candidate worth addressing directly. Go compiles to a single static binary by default, has fast compile times, a stable language (slow intentional churn), excellent AI training data, and trivial cross-compilation (`GOOS=linux GOARCH=amd64 go build`). For a companion app that only consumes web APIs, it would be a strong choice. The comparison below is specific to this app's actual requirements.

### The overlay is the deciding factor

The hardest requirement — a transparent, always-on-top window with **per-region hit-testing** (background passes through to game, UI elements interactive) — is the axis on which the two stacks diverge completely.

**Qt6**: `QWidget::setMask(QRegion)`, `Qt::WA_TranslucentBackground`, `Qt::FramelessWindowHint`, and `Qt::WindowStaysOnTopHint` are all first-class built-ins. The overlay is ~10 lines of setup with no platform-specific code and no external dependencies beyond Qt itself.

**Fyne**: Fyne abstracts the OS window at a level that does not expose layered windows, per-monitor-DPI topmost windows, or region-based hit-testing. A transparent always-on-top overlay with selective passthrough is **not achievable in pure Fyne**. Achieving it requires dropping to CGo and calling Win32 (`SetLayeredWindowAttributes`, `SetWindowRgn`, `WM_NCHITTEST`) and X11 (`XChangeProperty`, shape extension) directly.

Once CGo is involved the primary advantages of Go for distribution collapse:
- Static binary is no longer possible (CGo requires the C runtime)
- A C toolchain (MSVC or MinGW on Windows, GCC on Linux) becomes required to build
- Cross-compilation becomes significantly harder (CGo does not cross-compile easily)
- The overlay code is now essentially raw Win32/X11 with no framework help — the same wheel being reinvented that Qt solves

At that point the project has the complexity of C++ without the Qt API surface that makes that complexity worthwhile.

### Pros and cons table

| | **C++ + Qt6** | **Go + Fyne** |
|---|---|---|
| Overlay (transparent + per-region hit-test) | First-class (`setMask`, `WA_TranslucentBackground`) | Requires CGo + raw Win32/X11 |
| System tray | `QSystemTrayIcon` — integrated, main-thread, no hacks | `fyne.io/systray` — works but separate from Fyne's event loop, similar threading quirks to what `tray-icon` caused in Rust |
| Window lifecycle (hide/show/close) | `closeEvent`, `hide()`, `show()`, `raise()` — retained, one call each | Fyne's `Window` interface exposes `Hide()`/`Show()` but lifecycle hooks are limited; no `closeEvent` equivalent |
| Steam Deck / KDE native feel | Qt IS KDE — apps look at home on SteamOS desktop | Fyne uses its own OpenGL renderer with a Material-inspired theme; looks foreign on KDE |
| Cross-monitor DPI | Qt platform plugin sets `PER_MONITOR_AWARE_V2` correctly | Fyne has had DPI issues on multi-monitor Windows setups |
| Compile times | Slow (C++ / Qt MOC headers) | Fast |
| Distribution (no overlay) | DLLs + installer | Single static binary — simpler |
| Distribution (with CGo overlay) | DLLs + installer | Requires C runtime, no static binary, cross-compile breaks |
| Language for vibe coding | C++ (more ceremony, but AI handles Qt patterns well) | Go (simpler syntax, faster iteration) |
| AI training data for GUI | Large (Qt C++ is extremely well documented) | Moderate (Fyne is smaller ecosystem) |
| Long-term API stability | Qt6 core API stable for years; KDAB commercial backing | Fyne API has had breaking changes between minor versions |
| Companion-only mode (no overlay) | Qt still appropriate; just don't show the overlay window | Go + Fyne would be a good fit here |

### When Go + Fyne would be the right choice instead

If the overlay requirement were dropped — i.e. the app ran in companion-only mode consuming PoE web APIs with no game window integration — Go + Fyne would be genuinely competitive. The static binary, fast iteration, and Go's excellent HTTP/JSON stdlib would be real advantages with no CGo penalty. It is worth revisiting if the product direction shifts to companion-first.

---

## Decision: C++ + Qt

### Why this fits the specific constraints

**Overlay (the hardest requirement):**  
`QWidget::setMask(QRegion)` implements per-region hit-testing in one call. Unmasked transparent regions pass clicks through to the game. Masked regions (where UI widgets live) capture input normally. The mask is updated when the layout changes. Same process as the main app — no IPC, no separate binary.

```cpp
// Overlay window setup
overlay->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
overlay->setAttribute(Qt::WA_TranslucentBackground);

// Update hit regions when UI layout changes:
QRegion interactive;
interactive += QRegion(button->geometry());
interactive += QRegion(textInput->geometry());
overlay->setMask(interactive);
```

**System tray:**  
`QSystemTrayIcon` with `QMenu`. Left-click and right-click are distinct signals. Menu actions call `window->show()` directly. No background threads, no `request_repaint()` pokes, no shared mutex.

**Window lifecycle (hide-to-tray, re-open):**  
Override `QWidget::closeEvent`, call `event->ignore()` and `this->hide()`. Call `this->show()` and `this->raise()` from the tray action. These are single retained calls on a real window object — no diffing, no `CancelClose`, no duplicate state.

**Cross-monitor DPI:**  
Qt sets `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` correctly via its platform plugin on Windows. No manual call required.

**Steam Deck:**  
Steam Deck's desktop is KDE Plasma, which is built on Qt. Qt apps are a first-class citizen on SteamOS. Distribution as an AppImage is the standard format for user-installed software on SteamOS.

**Styling:**  
Qt Style Sheets (QSS) — CSS-like syntax, compiled at startup, no browser engine, no JS. Custom dark game-tool aesthetic is straightforward.

**Speed:**  
Native C++. Fast cold start. QPainter and OpenGL widget rendering are fast enough for a 60fps overlay.

### Deployment per platform

| Platform | Tool | Output |
|---|---|---|
| Windows | `windeployqt` + Inno Setup / NSIS | Installer |
| Linux / Steam Deck | `linuxdeployqt` → AppImage | Single AppImage file |
| macOS | `macdeployqt` | `.app` bundle |

### What to carry forward from the Rust prototype

- The **window tracking approach** (`FindWindowA` + `GetWindowRect` on Windows, x11rb tree walk on Linux) is correct and should be reproduced in C++ using the same Win32/X11 APIs or Qt's `QProcess`-based approach.
- The **config schema** (`AppConfig` in `config.rs`) maps directly to a `QSettings` or hand-rolled TOML structure.
- The **overlay-as-same-process** architecture is now achievable; the two-binary split was an artifact of the GLFW/winit incompatibility and is discarded.
- The **stdin IPC** is discarded — there is no separate overlay process to communicate with.

### What is discarded

- `egui` / `eframe` / `egui_overlay` — replaced by Qt widgets and QPainter
- `tray-icon` — replaced by `QSystemTrayIcon`
- The background tray thread and `Arc<Mutex<AppState>>` — replaced by Qt's signal/slot mechanism on the main thread
- The separate overlay binary and stdin JSON IPC
- The `ViewportCommand::Visible` diffing hack
- The manual `SetProcessDpiAwarenessContext` call

---

## Consequences

- C++ compile times are slower than Rust or Python for iteration. Mitigated by using precompiled headers and incremental builds (CMake + Ninja).
- Qt is LGPL 3.0 for the open-source version. The app must either comply with LGPL (allow relinking, ship sources or object files) or purchase a Qt commercial license. Dynamic linking (shipping DLLs) satisfies LGPL.
- `cxx-qt` or Rust FFI remains an option if performance-critical logic (e.g., packet parsing, data processing) is better written in Rust — call into a Rust static library from C++. Not needed at prototype stage.
- If the project moves to companion-only mode (web APIs, no overlay), the Qt app shell still applies; the overlay window simply isn't shown.
