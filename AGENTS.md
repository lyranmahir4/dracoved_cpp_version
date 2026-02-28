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

### Quickest way — use the build script

After any code change, run `do_build.bat` from the project root in CMD:

```bat
cd C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version
do_build.bat
```

This handles all steps in order (build → copy EXE → windeployqt → copy DLL → copy ephe) and updates both `build/` and `dist/` executables. Always run it manually in CMD — it will not work through a bash/POSIX shell layer.

---

### Manual steps (reference only)
Use Windows CMD (as in `README.md`). Update Qt paths for your local machine.

- One-time configure (only if `dracoved_app/build` is missing):
  `set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"`
  `cmake -S C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app -B C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64`

- Required after code changes: build updated executable in `dracoved_app/build`:
  `set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"`
  `cmake --build C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build`

- Required after build: package fresh runtime files into `dracoved_app/dist`:
  `set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"`
  `copy /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build\dracoved_app.exe C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe`
  `"%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe`
  `copy /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\swedll64.dll C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\swedll64.dll`
  `xcopy /E /I /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\ephe C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\ephe`

- Run the app from `dist` (preferred runtime path):
  `C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe`

- Common pitfalls:
  `windeployqt` requires the EXE path argument.
  `copy` requires both source and destination paths.
  `swedll64.dll` and `ephe` are runtime assets, not executable commands.

- Build discipline (learned from real failures):
  Do: run commands in `cmd.exe` from `C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version`.
  Do: keep strict order after code changes: build -> copy `build\dracoved_app.exe` to `dist\dracoved_app.exe` -> `windeployqt <dist exe>` -> copy `swedll64.dll` -> `xcopy ephe`.
  Do: rerun the full packaging sequence if any step fails or the session is interrupted.
  Do: verify timestamps for both EXEs before testing from `dist`.
  Do not: omit mandatory command arguments (`windeployqt` target exe, `copy` destination path).
  Do not: execute `swedll64.dll` or `ephe\` as commands.
  Do not: assume build success updates `dist`; packaging is required.
  Do not: test from `dist` if only `build` was refreshed.

- Policy: after any code change, both EXEs must be current:
  - `dracoved_app/build/dracoved_app.exe`
  - `dracoved_app/dist/dracoved_app.exe`

## Coding Style & Naming Conventions
- Language/tooling baseline: C++20 with Qt 6 Widgets + Network.
- Follow existing style in surrounding files; keep changes localized and minimal.
- Indentation: 4 spaces.
- File names: `snake_case` (example: `chart_wheel_widget.cpp`).
- Class/type names: `PascalCase`.
- Keep related `.h` and `.cpp` updates in sync.
- Keep app code under the `dracoved` namespace where already used.
- No formatter is configured; avoid unrelated formatting-only diffs.

## Product UX/Data Rules
- Use status bar/non-modal feedback for normal info/success flows.
- Reserve modal popups for critical blocking failures.
- In transit tabs, selected row should drive chart state and detail pane state.
- Calendar and Scan detail panes should present Placements + Summary for selected moments.
- Keep copy/report parity across Search, Calendar, Conjunction, Scan, and Lunation where selection exists.
- For conjunctions: with exactly 2 planets selected and `N=2`, treat as exact pair mode (0 degree span) and disable orb controls in UI.
- Keep Arabic Lots visible by default, but provide visibility controls for readability.
- Keep fixed-star rendering legible under high density (collision-aware placement or suppression).

## Testing & Validation
- No automated test framework is currently wired in.
- Primary validation is a successful configure/build of `dracoved_app` and a quick manual run of the packaged executable.
- For calculation-sensitive changes, verify at least:
  - natal chart generation
  - transits workflow (Search/Calendar/Conjunction/Scan/Lunation)
  - solar return workflow
- For transit UX changes, verify:
  - row selection updates chart state
  - bottom-right detail updates for selected moment
  - copy output exists for active supported subtab
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
