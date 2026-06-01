# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build . --parallel
```

Optional CMake flags:
- `-DTESTING=ON` — build and register tests
- CLI11 is auto-fetched on macOS/Windows; on Linux it's optional (`-DSANE_BREAK_CLI_CONTROL_ENABLED` is set automatically based on whether `CLI11` is found)
- Layer shell (Wayland) support is conditionally compiled when `layer-shell-qt` is present

## Tests

Tests require `libgmock-dev` and use Qt Test + Google Mock.

```bash
# Build and run all tests
cmake -DTESTING=ON ..
cmake --build . --parallel --target check

# Headless (required on systems without a display)
QT_QPA_PLATFORM=offscreen ctest --verbose --output-on-failure
```

Screenshot harness (not part of `check`):
```bash
cmake --build . --target screenshot
```

## Formatting

Pre-commit hooks enforce formatting. Run manually:
```bash
pre-commit run --all-files
```

- C++: `clang-format` (`.clang-format` at root)
- CMake: `cmake-format` (`.cmake-format.py` at root)
- JS/CSS: Biome
- Python: ruff

Every `.cpp`/`.h` file must begin with the GPL-3.0 license header from `.github/license_header.txt`. The `insert-license` pre-commit hook adds it automatically.

## Architecture

The project is split into four static libraries and one executable:

| Target | Path | Role |
|---|---|---|
| `sane-core-idle` | `src/core/idle-time.h` | Minimal idle-time interface, shared with `sane-idle` |
| `sane-core` | `src/core/` | Platform-agnostic business logic and state machine |
| `sane-idle` | `src/idle/` | Platform-specific idle-time detection |
| `sane-lib` | `src/lib/` | Platform-specific system integrations (battery, auto-start, screen lock, sleep, program monitor) |
| `sane-gui` | `src/app/` | Qt widgets, tray, windows, sound |
| `sane-break-app` | `src/app/main.cpp` | Final executable |

Dependency order: `sane-core` ← `sane-idle` ← `sane-lib` ← `sane-gui` ← `sane-break-app`

### State machine (`src/core/app-states.h`)

`AppContext` owns a `std::unique_ptr<AppState>` and dispatches events to it. States:

- `AppStateNormal` — actively counting down to next break
- `AppStatePaused` — countdown suspended (idle, battery, meeting, focus mode, etc.)
- `AppStateBreak` — active break, delegates to a `BreakPhase`
  - `BreakPhasePrompt` — flashing heads-up window (phase 1)
  - `BreakPhaseFullScreen` — full-screen overlay (phase 2)
- `AppStatePostBreakIdle` — brief window after a break completes
- `AppStateMeeting` — break schedule suspended during a meeting/presentation

User-initiated commands travel as `MenuAction` variants (defined in the `Action` namespace). System/platform events (idle start/end, sleep, pause/resume) go through virtual methods on `AppState`.

### Core vs GUI split

`AbstractApp` (`src/core/app.h`) is the testable core. `SaneBreakApp` (`src/app/app.h`) extends it with actual Qt windows and is not tested directly. Dependencies (timers, idle detection, break windows, meeting prompt, DB) are injected via `AppDependencies`.

### Platform-specific code

Each platform provides concrete implementations under `src/idle/linux/`, `src/idle/macos-idle.h`, `src/lib/linux/`, `src/lib/macos/`, `src/lib/windows/`, etc. Linux additionally has:
- `src/app/layer-shell/` — wlr-layer-shell Wayland protocol integration
- `src/lib/linux/wayland-check/` — runtime detection of Wayland compositor capabilities
- GNOME idle via D-Bus (`src/idle/linux/gnome/`)
- Wayland idle via `ext-idle-notify-v1` (`src/idle/linux/wayland/`)
- X11 idle via `libXss` (`src/idle/linux/x11/`)
