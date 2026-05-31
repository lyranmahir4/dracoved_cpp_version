# DracoVed C++

DracoVed C++ is an **advanced astrology research application** for Windows, built with **C++20**, **Qt 6 Widgets**, and **Swiss Ephemeris**. It combines dense chart calculation workflows with research-oriented transit tools, synchronized detail panes, and AI-assisted analysis features for interpreting and organizing astrological results.

The goal is to provide a fast native desktop tool for natal chart work, sidereal/tropical switching, transit research, return charts, progressions, relocation workflows, and detailed copyable reports that can support deeper astrology research and analysis.

## Highlights

- Tropical and sidereal natal charts using Swiss Ephemeris.
- Whole Sign and Placidus house support.
- App-wide zodiac controls for tropical/sidereal mode and sidereal ayanamsa selection.
- Chart wheel with zoom, pan, reset, aspect lines, tooltips, and overlay modes.
- Fixed stars support with collision-aware label placement.
- Arabic Lots and derived points support, including Rhetorius-focused lot workflows.
- Transit tools for search, calendar views, conjunction scans, best-days scans, and lunation/eclipses.
- AI-assisted analysis tools for turning chart, transit, and report data into structured interpretive research notes.
- Detail/report panes with copy-to-clipboard support.
- Annual profection and lord-of-year style workflow support.
- Solar Return, Progression, and Relocation workflows.
- Location geocoding through OpenStreetMap Nominatim.
- Timezone lookup through Open-Meteo.
- Profile save/load/delete support using JSON and Qt settings.

## Tech Stack

- C++20
- Qt 6 Widgets and Qt Network
- Swiss Ephemeris
- CMake
- Ninja
- Windows 11

## Repository Layout

```text
dracoVed_cpp_version/
├── dracoved_app/        # Main Qt/CMake application source
├── ephe/                # Swiss Ephemeris data files used at runtime
├── sweph/               # Vendored Swiss Ephemeris source/tools
├── do_build.bat         # Local Windows build/package helper
└── README.md
```

Local build and packaging outputs should stay out of version control. The repository now includes a `.gitignore` for common CMake, Qt, Windows, and local runtime artifacts.

## Runtime Assets

The app expects Swiss Ephemeris data files to be available at runtime. The current project uses the following files for the 1800-2399 range:

```text
sepl_18.se1
semo_18.se1
seas_18.se1
sefstars.txt
```

Optional future-range files may also be used:

```text
sepl_24.se1
semo_24.se1
seas_24.se1
```

The Swiss Ephemeris DLL must be available next to the packaged executable or through the `DRACOVED_SWE_DLL` environment variable.

## Build and Run on Windows

### Prerequisites

Install:

- Qt 6 for MinGW
- CMake
- Ninja
- A compatible MinGW toolchain

Make sure the Qt, CMake, Ninja, and MinGW `bin` directories are available on `PATH`.

### Configure

From the repository root:

```bat
cmake -S dracoved_app -B dracoved_app\build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.10.1\mingw_64
```

Adjust `CMAKE_PREFIX_PATH` if your Qt version or install path is different.

### Build

```bat
cmake --build dracoved_app\build
```

### Package a Local Runtime

```bat
set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"
if not exist dracoved_app\dist mkdir dracoved_app\dist
copy /Y dracoved_app\build\dracoved_app.exe dracoved_app\dist\dracoved_app.exe
"%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations dracoved_app\dist\dracoved_app.exe
copy /Y swedll64.dll dracoved_app\dist\swedll64.dll
xcopy /E /I /Y ephe dracoved_app\dist\ephe
```

### Run

```bat
dracoved_app\dist\dracoved_app.exe
```

### Quick Local Build Helper

The repository includes `do_build.bat` for local development. It is intended to run the configure/build/package flow in order and refresh the packaged runtime after code changes.

## Current Status

This repository has recently been made public. The next public-release tasks are:

- Add screenshots to the README.
- Publish the first GitHub Release with a packaged Windows build.
- Confirm and document the project license.
- Move generated build/package artifacts out of the tracked source tree.
- Add a short architecture overview for contributors.
- Document the AI analysis workflow and any required configuration.

## Contributing

Issues and pull requests are welcome. Good first contributions include:

- Build fixes for different Qt/MinGW versions.
- UI polish and accessibility improvements.
- More complete documentation.
- Test cases for date, timezone, chart, transit, and AI-analysis edge cases.
- Packaging/release automation.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for contribution guidelines.

## Maintenance Notes

After code changes, rebuild and redeploy the runtime before testing from `dist`:

```text
cmake --build -> copy EXE -> windeployqt -> copy Swiss Ephemeris DLL -> copy ephe assets
```

Avoid mixing old and new runtime files in `dist`; redeploy the full runtime after each build.

## License

The project license still needs to be finalized. Because this project depends on Swiss Ephemeris assets/code, confirm the correct licensing approach before distributing binaries or accepting outside contributions.
