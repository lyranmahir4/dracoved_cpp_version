# Repository Guidelines

## Project Structure & Module Organization
- `dracoved_app/` contains the Qt 6 application.
- `dracoved_app/src/core/` holds calculation, ephemeris, time, and formatting logic.
- `dracoved_app/src/gui/` holds widgets, dialogs, workers, and UI orchestration.
- `dracoved_app/build/` is local CMake build output.
- `dracoved_app/dist/` is the packaged runtime folder (EXE + Qt DLLs + ephemeris + Swiss Ephemeris DLL).
- `ephe/` stores Swiss Ephemeris data files (`.se1`, `sefstars.txt`).
- `sweph/` is upstream Swiss Ephemeris vendor source/tools and should be treated as external code.

## Build, Run, and Packaging Commands
Use Windows CMD (as in `README.md`). Update Qt paths for your local machine.

- Configure (one-time):
  `cmake -S DracoVed_cpp_version\dracoved_app -B DracoVed_cpp_version\dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64`
- Build:
  `cmake --build DracoVed_cpp_version\dracoved_app\build`
- Package:
  run `windeployqt` and copy runtime dependencies/data exactly as documented in `README.md`.
- Run packaged app:
  `DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe`

## Coding Style & Naming Conventions
- Language/tooling baseline: C++20 with Qt 6 Widgets + Network.
- Follow existing style in surrounding files; keep changes localized and minimal.
- Indentation: 4 spaces.
- File names: `snake_case` (example: `chart_wheel_widget.cpp`).
- Class/type names: `PascalCase`.
- Keep related `.h` and `.cpp` updates in sync.
- Keep app code under the `dracoved` namespace where already used.
- No formatter is configured; avoid unrelated formatting-only diffs.

## Testing & Validation
- No automated test framework is currently wired in.
- Primary validation is a successful configure/build of `dracoved_app` and a quick manual run of the packaged executable.
- For calculation-sensitive changes, verify at least:
  natal chart generation, transits workflow, and solar return workflow.
- If you add tests/scripts, place them near the affected module and document how to run them.

## Commit & Pull Request Guidelines
- Use a clear, imperative commit summary (optional `type:` prefix is fine, e.g., `fix:`, `feat:`).
- Keep commits focused; avoid mixing refactors with behavior changes unless necessary.
- PRs should include:
  concise summary, build/run notes, and screenshots for UI-facing changes.
- Link issues/tasks when relevant.

## Configuration & Runtime Assets
- Required ephemeris files for 1800-2399 in `ephe/`:
  `sepl_18.se1`, `semo_18.se1`, `seas_18.se1`, `sefstars.txt`.
- `swedll64.dll` must be present beside the packaged EXE in `dracoved_app/dist/`,
  or provided via `DRACOVED_SWE_DLL`.
- If ephemeris or DLL assets are missing, calculations may fail or return incomplete results.
