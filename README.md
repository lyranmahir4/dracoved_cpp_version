# DracoVed C++ (Windows 11)

DracoVed C++ is a desktop astrology workstation built with Qt 6 and Swiss Ephemeris. The app is optimized for dense, data-first workflows where chart state, transit results, and detail panes stay synchronized.

## Design Philosophy

- Dense, information-rich UI with fast interaction loops.
- Data-first transit UX: selected results should immediately be inspectable in chart + detail/report views.
- Non-blocking status for normal operations, modal dialogs only for critical failures.
- Practical rendering controls to reduce clutter without hiding important data.

## Current Features

- Tropical natal charts (Swiss Ephemeris), Whole Sign or Placidus houses.
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
  - Lunation search.
- Transit detail/report parity:
  - Search, Calendar, Conjunction, Scan, and Lunation all support right-bottom detail reporting and copy-to-clipboard output when selection is active.
  - Calendar and Scan detail panes show Placements + Summary for selected moments.
- Profection/activation workflow support for annual sign/lord-of-year style analysis.
- Solar Return, Progression, and Relocation workflows.
- Location geocoding (OpenStreetMap Nominatim) and timezone lookup (Open-Meteo).
- Profile save/load/delete (JSON) with settings persisted via QSettings.

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

### One-time configure (only if `dracoved_app/build` is missing)

```bat
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
cmake -S DracoVed_cpp_version\dracoved_app -B DracoVed_cpp_version\dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64
```

### Build updated EXE in `build/` (required after code changes)

```bat
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
cmake --build DracoVed_cpp_version\dracoved_app\build
```

### Package fresh runtime EXE in `dist/` (required after build)

```bat
set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"
mkdir DracoVed_cpp_version\dracoved_app\dist 2>NUL
copy /Y DracoVed_cpp_version\dracoved_app\build\dracoved_app.exe DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
"%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
copy /Y DracoVed_cpp_version\swedll64.dll DracoVed_cpp_version\dracoved_app\dist\swedll64.dll
xcopy /E /I /Y DracoVed_cpp_version\ephe DracoVed_cpp_version\dracoved_app\dist\ephe
```

### Run packaged app (preferred)

```bat
DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
```

## Notes

- Always refresh both executables after code changes: `dracoved_app/build/dracoved_app.exe` and `dracoved_app/dist/dracoved_app.exe`.
- Use accurate local time and timezone inputs for reliable transit/profection timing.
- Chart wheel orientation follows Astro.com style (Ascendant left, MC top).
