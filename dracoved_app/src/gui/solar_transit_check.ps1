param([ValidateSet('algorithms', 'activity', 'ephemeris', 'ui', 'all')][string]$Mode = 'all', [switch]$AllMajorAspects)

# Standalone checks only. Does not build/package the main application.
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:\Qt\6.10.1\mingw_64'
$compiler = 'C:\Qt\Tools\mingw1310_64\bin\g++.exe'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qtRoot\bin;" + $env:PATH
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
Push-Location -LiteralPath $projectRoot
try {
    if ($Mode -in @('all', 'algorithms')) {
        & $compiler -std=c++20 dracoved_app/src/gui/solar_transit_tests.cpp -o dracoved_app/build/solar_transit_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Algorithm test compilation failed.' }
        & .\dracoved_app\build\solar_transit_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Algorithm tests failed.' }
    }
    if ($Mode -in @('all', 'activity')) {
        & $compiler -std=c++20 "-I$qtRoot/include" "-I$qtRoot/include/QtCore" dracoved_app/src/gui/solar_transit_activity_tests.cpp "-L$qtRoot/lib" -lQt6Core -o dracoved_app/build/solar_transit_activity_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Activity test compilation failed.' }
        & .\dracoved_app\build\solar_transit_activity_tests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Activity tests failed.' }
    }
    if ($Mode -in @('all', 'ephemeris', 'ui')) {
        # Reuse existing core objects from the user's normal build.
        $ninja = Get-Content dracoved_app/build/build.ninja
        $includes = (($ninja | Select-String '^  INCLUDES = ' | Select-Object -First 1).Line -replace '^  INCLUDES = ', '') -split ' '
        $coreObjects = Get-ChildItem dracoved_app/build/CMakeFiles/dracoved_app.dir/src/core -Filter '*.obj' | ForEach-Object { $_.FullName }
        if (!$coreObjects) { throw 'Run the normal application build once to provide the core object dependencies.' }
        $common = @('dracoved_app/src/gui/solar_transit_calculation.cpp') + $coreObjects + @(
            'dracoved_app/build/CMakeFiles/dracoved_app.dir/src/gui/return_calculation_service.cpp.obj',
            'dracoved_app/build/CMakeFiles/dracoved_app.dir/src/gui/transit_calc_service.cpp.obj')
        if ($Mode -in @('all', 'ephemeris')) {
            & $compiler -std=c++20 -O1 @includes dracoved_app/src/gui/solar_transit_ephemeris_tests.cpp @common "-L$qtRoot/lib" -lQt6Gui -lQt6Core -o dracoved_app/build/solar_transit_ephemeris_tests.exe
            if ($LASTEXITCODE -ne 0) { throw 'Ephemeris test compilation failed.' }
            & .\dracoved_app\build\solar_transit_ephemeris_tests.exe
            if ($LASTEXITCODE -ne 0) { throw 'Ephemeris tests failed.' }
        }
        if ($Mode -in @('all', 'ui')) {
            & $compiler -std=c++20 -O1 @includes dracoved_app/src/gui/solar_transit_ui_tests.cpp dracoved_app/src/gui/solar_transit_panel.cpp dracoved_app/src/gui/solar_transit_activity_panel.cpp dracoved_app/build/dracoved_app_autogen/MXUWEOXILK/moc_chart_wheel_widget.cpp @common dracoved_app/build/CMakeFiles/dracoved_app.dir/src/gui/chart_wheel_widget.cpp.obj "-L$qtRoot/lib" -lQt6Widgets -lQt6SvgWidgets -lQt6Svg -lQt6Gui -lQt6Core -o dracoved_app/build/solar_transit_ui_tests.exe
            if ($LASTEXITCODE -ne 0) { throw 'UI test compilation failed.' }
            $previousPlatform = $env:QT_QPA_PLATFORM
            try {
                $env:QT_QPA_PLATFORM = 'offscreen'
                $uiArguments = @()
                if ($AllMajorAspects) { $uiArguments += '--all-major' }
                & .\dracoved_app\build\solar_transit_ui_tests.exe @uiArguments
                if ($LASTEXITCODE -ne 0) { throw 'UI tests failed.' }
            } finally { $env:QT_QPA_PLATFORM = $previousPlatform }
        }
    }
} finally { Pop-Location }
