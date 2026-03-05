# DracoVed C++ (Windows 11)

DracoVed C++ is a desktop astrology workstation built with Qt 6 and Swiss Ephemeris. The app is optimized for dense, data-first workflows where chart state, transit results, and detail panes stay synchronized.

## Design Philosophy

- Dense, information-rich UI with fast interaction loops.
- Data-first transit UX: selected results should immediately be inspectable in chart + detail/report views.
- Non-blocking status for normal operations, modal dialogs only for critical failures.
- Practical rendering controls to reduce clutter without hiding important data.

## Current Features

- Tropical and Sidereal natal charts (Swiss Ephemeris), Whole Sign or Placidus houses.
- Global zodiac mode controls in the top bar:
  - `Tropical` or `Sidereal` toggle.
  - Sidereal ayanamsa selection (`Lahiri`, `Raman`, `Krishnamurti`, `Fagan/Bradley`, `Yukteshwar`, `True Citra`, `True Revati`).
  - Selected mode applies app-wide to placements/transits calculations.
- Chart wheel with zoom/pan/reset, aspect lines, tooltips, and overlay modes.
- Fixed stars support with display toggle and collision-aware label placement.
- Arabic Lots and derived points support with chart visibility toggles:
  - Lots are visible by default.
  - Derived points are visible by default.
- Rhetorius-focused lots framework, including:
  - Fortune and Spirit (day/night reversal).
  - Action, Brothers, Father.
  - Planetary/Pauline lots.
  - Marriage lot with natal gender-aware formula path (male/female/unspecified profile field).
- Transit tools:
  - Transit Search (including exact degree-in-sign targeting).
  - Transit Calendar.
  - Conjunction finder (including exact 0 degree two-planet mode when `N=2` and two planets selected).
  - Best Days scan.
  - Lunation search with sidereal-aware eclipse rule selection:
    - `Astronomical (Swiss)` rule (physical eclipse events from Swiss Ephemeris).
    - `Strict Vedic (whole-sign nodes)` rule (sidereal-only): classifies eclipses from exact new/full moon instants using whole-sign Rahu/Ketu axis conditions.
  - In sidereal mode, lunation house-based analysis/filtering uses whole-sign logic.
- Transit detail/report parity:
  - Search, Calendar, Conjunction, Scan, and Lunation all support right-bottom detail reporting and copy-to-clipboard output when selection is active.
  - Calendar and Scan detail panes show Placements + Summary for selected moments.
- Profection/activation workflow support for annual sign/lord-of-year style analysis.
- Solar Return, Progression, and Relocation workflows.
- Location geocoding (OpenStreetMap Nominatim) and timezone lookup (Open-Meteo).
- Profile save/load/delete (JSON) with settings persisted via QSettings.
  - Profiles include zodiac system and sidereal ayanamsa fields.

## Tech Stack

- C++20
- Qt 6 Widgets + Network
- Swiss Ephemeris (`swedll64.dll` + `.se1` files)
- CMake + Ninja (Windows)

## Project Layout

- `dracoved_app/` source + CMake project
- `dracoved_app/build/` local build output (updated EXE)
- `dracoved_app/dist/` packaged runtime (EXE + Qt DLLs + ephemeris + Swiss DLL)
- `ephe/` Swiss Ephemeris data files
- `sweph/` upstream Swiss Ephemeris source/tools (vendor code)

## Runtime Assets

Required ephemeris files for 1800-2399 in `DracoVed_cpp_version/ephe`:

- `sepl_18.se1`
- `semo_18.se1`
- `seas_18.se1`
- `sefstars.txt`

Optional future range files:

- `sepl_24.se1`
- `semo_24.se1`
- `seas_24.se1`

Swiss Ephemeris DLL:

- `swedll64.dll` must be next to the packaged EXE in `dracoved_app/dist/`, or provided via `DRACOVED_SWE_DLL`.

## Build and Run (Windows CMD)

### Quickest way — use the build script

After any code change, just double-click `do_build.bat` in the project root, or run it from CMD:

```bat
cd C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version
do_build.bat
```

This runs all required steps in order: build → copy EXE to `dist/` → `windeployqt` → copy `swedll64.dll` → copy `ephe/`. Both `build/` and `dist/` executables will be current when it finishes.

---

### Manual steps (reference only)

Run these in `cmd.exe` from:
`C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version`

### One-time configure (only if `dracoved_app/build` is missing)

```bat
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
cmake -S C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app -B C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64
```

### Build updated EXE in `build/` (required after code changes)

```bat
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
cmake --build C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build
```

### Package fresh runtime EXE in `dist/` (required after build)

```bat
set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"
copy /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build\dracoved_app.exe C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
"%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
copy /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\swedll64.dll C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\swedll64.dll
xcopy /E /I /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\ephe C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\ephe
```

### Run packaged app (preferred)

```bat
C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
```

### Common pitfalls

- `windeployqt` must include the EXE path argument.
- `copy` must include both source and destination paths.
- `swedll64.dll` and `ephe` are assets; do not execute them as commands.

### Build Discipline (Do / Do Not)

Do:
- Run build/package commands in `cmd.exe`, not mixed through another shell parser.
- Run from `C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version`.
- Run steps in this order every time after code changes:
  `cmake --build` -> copy `build\dracoved_app.exe` to `dist\dracoved_app.exe` -> `windeployqt <dist exe>` -> copy `swedll64.dll` -> `xcopy ephe`.
- If any step fails or the session is interrupted, rerun the full packaging sequence before testing.
- Verify both exe timestamps are current before launching from `dist`.

Do not:
- Do not omit required arguments (`windeployqt` target exe, `copy` destination path).
- Do not treat assets (`swedll64.dll`, `ephe\`) as executable commands.
- Do not assume a successful build means `dist` is updated; packaging is a separate required step.
- Do not mix partial old/new runtime files in `dist`; always redeploy after a new build.

## Notes

- Always refresh both executables after code changes: `dracoved_app/build/dracoved_app.exe` and `dracoved_app/dist/dracoved_app.exe`.
- Use accurate local time and timezone inputs for reliable transit/profection timing.
- Chart wheel orientation follows Astro.com style (Ascendant left, MC top).
