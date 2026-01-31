# DracoVed C++ (Windows 11)

DracoVed C++ is a fast, desktop-focused port of the DracoVed app built with Qt 6. The current MVP focuses on Tropical natal charts with a dense, dashboard-style UI.

## Design Philosophy

- Dense, information-rich UI inspired by trading terminals and Astro.com layouts.
- Light default with an optional dark theme and subtle accent highlights for focus and clarity.
- Fast, responsive calculations and rendering using Swiss Ephemeris.
- Modern UI polish with practical interactions (zoom, pan, tooltips).

## Current Features (MVP)

- Tropical natal chart calculations (Swiss Ephemeris).
- Whole Sign or Placidus houses.
- Aspect matrix with glyphs/abbrev/full labels, colors, and orbs.
- Pyramid (triangular) aspect grid for symmetric charts with axis hover highlights.
- Chart wheel with sign ring, degree ticks, house lines, aspect lines, and planet glyphs + degrees.
- Zoom, pan (middle mouse), and reset for the chart wheel.
- Tooltips on planets/angles and aspect cells.
- Location geocoding (OpenStreetMap Nominatim).
- Automatic timezone lookup after geocoding (Open-Meteo).
- Profile save/load/delete (JSON).
- Theme menu with Light/Dark modes.
- Transits tab with overlay or transit-only modes, aspect scope switching, and transit search.
- Solar Return tab with year input, natal/custom location, Solar Return aspects, and Solar–Natal aspects.
- Solar Return inputs persisted via QSettings.

## Coming Next

- Vedic (sidereal) mode.
- Right-side info panels (elements, modalities, retrogrades).
- Better chart collision handling for planet labels.
- Chart export (PNG/PDF).
- Synastry, progressions, and advanced charts.

## Tech Stack

- C++20
- Qt 6 Widgets + Network
- Swiss Ephemeris (DLL + .se1 data files)
- CMake + Ninja (Windows)

## Project Layout

- dracoved_app/        C++ source and build output
- dracoved_app/build   Build output
- dracoved_app/dist    Packaged EXE + Qt DLLs + ephe data
- ephe/                Swiss Ephemeris data files
- sweph/               Swiss Ephemeris source + tools

## Ephemeris Files Needed

For current dates (1800-2399), place these in:
DracoVed_cpp_version/ephe

- sepl_18.se1
- semo_18.se1
- seas_18.se1
- sefstars.txt

Optional (future years):
- sepl_24.se1
- semo_24.se1
- seas_24.se1

Swiss Ephemeris DLL:
- swedll64.dll must be next to the EXE (dist) or set via DRACOVED_SWE_DLL.

## Build and Run (Windows CMD)

### One-time configure (only if build folder is missing)

```
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
cmake -S DracoVed_cpp_version\dracoved_app -B DracoVed_cpp_version\dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64
```

### Build (repeat any time)

```
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
cmake --build DracoVed_cpp_version\dracoved_app\build
```

### Package dist EXE (repeat any time)

```
set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"
mkdir DracoVed_cpp_version\dracoved_app\dist 2>NUL
copy /Y DracoVed_cpp_version\dracoved_app\build\dracoved_app.exe DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
"%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
copy /Y DracoVed_cpp_version\swedll64.dll DracoVed_cpp_version\dracoved_app\dist\swedll64.dll
xcopy /E /I /Y DracoVed_cpp_version\ephe DracoVed_cpp_version\dracoved_app\dist\ephe
```

### Run the packaged EXE

```
DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe
```

## Notes

- Enter local time with a correct timezone (e.g., Asia/Dhaka or UTC+6).
- Geocode fills lat/lon and auto-sets timezone.
- The chart wheel orientation follows Astro.com style (Ascendant at left, MC at top).
- Solar Return defaults to natal location unless a custom location is selected.
