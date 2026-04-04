# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ImageTool** — a cross-platform desktop image resizing application written in C/C++ with Dear ImGui + GLFW + OpenGL for the GUI and stb_image libraries for image processing.

## Build Commands

Dependencies are fetched automatically via CMake `FetchContent` (GLFW 3.4, Dear ImGui v1.91.9b, stb).

**macOS:**
```bash
cmake --preset macos
cmake --build --preset build-macos
# Output: build/macos/ImageTool
```

**Windows (Visual Studio 2022):**
```powershell
cmake --preset win64
cmake --build --preset build-win64
# Output: build\win64\Release\ImageTool.exe
```

There is no test suite — CI runs build verification on push/PR via `.github/workflows/ci.yml`.

## Architecture

The codebase has a strict two-layer design:

**C core (`core/`)** — Pure C11 image processing library compiled as a static library (`image_core`). Public API in `image_core.h`: `img_load_rgba()`, `img_resize_rgba()`, `img_save_png()`, `img_save_jpg()`, `img_save_pdf()`, `img_free()`. The `Image` struct holds `{w, h, channels, pixels*}`. Callers must call `img_free()` to release pixel buffers.

**C++ UI layer (`app/`)** — C++17 ImGui application compiled into the `ImageTool` executable. Key files:
- `main.cpp` — Entry point only; instantiates `Application` and calls `run()`
- `application.h/cpp` — `Application` class: GLFW/ImGui lifecycle (`init_glfw`, `init_imgui`, `shutdown`), render loop (`run`, `render_frame`), and GLFW callbacks (`drop_callback`) as static methods
- `ui.cpp/h` — All ImGui rendering, theming (purple/cyan scheme), font loading (Japanese support via merged fonts)
- `image_session.cpp/h` — High-level session management: file browsing, batch resize, image selection
- `file_dialogs.cpp/h` — Platform-specific native file dialog wrappers
- `app_state.h` — Shared `AppState` struct (file browser state, current `Image`, `GLTexture` preview, resize params, output settings)

## Platform Differences

- **macOS**: Uses `-framework ApplicationServices` for native PDF and HEIC/HEIF support via CoreImage/ImageIO. GLSL version: Core Profile 3.2. GL deprecation warnings suppressed.
- **Windows**: Uses `Comdlg32`, `Ole32`, `Shell32` for native file dialogs. GLSL version: Compatibility 3.0.
- HEIC support is macOS-only; on Windows, HEIC files will not load.

## Key Implementation Notes

- `AppState` is the single shared state struct passed through the UI layer — avoid scattering state elsewhere.
- `GLTexture` inside `AppState` holds the OpenGL handle for image preview; must be updated whenever the loaded image changes.
- Memory: pixel buffers are heap-allocated by stb; always call `img_free()` before replacing or discarding an `Image`.
- Buffer overflow safety: SIZE_MAX and INT_MAX bounds checks are used throughout `image_core.c` — maintain these when modifying save/resize paths.
- EXIF orientation is not handled.

## Coding Conventions

### General
- **OOP first**: New functionality in `app/` must be encapsulated in a class. Free functions are only acceptable for stateless utilities or C-compatible APIs in `core/`.
- `main.cpp` must stay minimal — entry point only. No logic, no includes beyond `application.h`.
- New source files go in `app/` (C++ UI layer) or `core/` (C image processing). Add them to `CMakeLists.txt` under `ImageTool` or `image_core` accordingly.

### C++ Style (`app/`)
- Standard: C++17. No extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- Member variables use `trailing_underscore_` naming.
- Class method order in `.h`: public → private. Constructor/destructor first.
- Disable copy/move on classes that own OS/GPU resources (GLFW window, OpenGL textures) with `= delete`.
- Prefer `const` references for read-only parameters; avoid raw pointers unless interfacing with a C API.
- Use `std::filesystem::path` (aliased as `fs::path`) for all file paths — never raw strings.
- `ImGuiIO`, `GLFWwindow*`, and OpenGL state are owned by `Application`; do not access them from outside that class.

### C Style (`core/`)
- Standard: C11. No extensions.
- All public functions are prefixed `img_`.
- Output parameters use pointer-to-struct (e.g., `Image* out`).
- Every allocation must have a matching `img_free()` call path visible at the call site.

### Formatting
- 2-space indentation throughout.
- Opening braces on the same line as the statement.
- `#include` order: own header first, then system/third-party headers in alphabetical order.
- Compiler warnings (`-Wall -Wextra -Wpedantic` / `/W4 /permissive-`) must remain clean — no suppressions without a comment explaining why.
