# Repository Guidelines

## Project Structure & Module Organization
- `dracoved_app/` contains the Qt 6 application. Source lives in `dracoved_app/src/` with `gui/` for widgets/dialogs and `core/` for calculation/formatting logic.
- `dracoved_app/build/` is the CMake build output; `dracoved_app/dist/` is the packaged EXE + Qt DLLs + ephemeris data.
- `ephe/` stores Swiss Ephemeris data files (`.se1`, `sefstars.txt`).
- `sweph/` is the Swiss Ephemeris upstream source and tools (vendor code).
- `qt_sanity_test/` is a minimal Qt Widgets app to validate toolchain setup.

## Build, Test, and Development Commands
Use Windows CMD (as in `README.md`). Update Qt paths as needed.
- Configure (one-time):
  `cmake -S DracoVed_cpp_version\dracoved_app -B DracoVed_cpp_version\dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64`
- Build: `cmake --build DracoVed_cpp_version\dracoved_app\build`
- Package: run `windeployqt` and copy DLL/data (see `README.md` packaging block).
- Run packaged app: `DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe`
- Optional Qt sanity check:
  `cmake -S DracoVed_cpp_version\qt_sanity_test -B DracoVed_cpp_version\qt_sanity_test\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64`

## Coding Style & Naming Conventions
- C++20, Qt 6 Widgets + Network; keep changes compatible with existing Qt usage.
- Indentation: 4 spaces; keep braces and include ordering consistent with surrounding files.
- Files use `snake_case` (e.g., `chart_wheel_widget.cpp`), classes use `PascalCase`.
- Prefer paired `.h/.cpp` updates and keep code inside the `dracoved` namespace where applicable.
- No formatter is configured; keep diffs minimal and localized.

## Testing Guidelines
- No automated test framework is currently wired in.
- Validate changes by building `dracoved_app` and (optionally) `qt_sanity_test`.
- If you add tests, document how to run them and keep them close to the affected module.

## Commit & Pull Request Guidelines
- Commit history is mixed (e.g., `feat: ...`, `Add ...`, and ad-hoc messages). Use a clear, imperative summary; optional `type:` prefixes are welcome.
- PRs should include: a concise summary, build/run notes, and screenshots for UI changes. Link issues when relevant.

## Configuration & Assets
- Required ephemeris files for 1800-2399 live in `ephe/` (`sepl_18.se1`, `semo_18.se1`, `seas_18.se1`, `sefstars.txt`).
- `swedll64.dll` must sit next to the EXE in `dracoved_app/dist/` or be pointed to via `DRACOVED_SWE_DLL`.
