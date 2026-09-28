# One focused reference-calculation/UI check; does not build or package the app.
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:/Qt/6.10.1/mingw_64'
$compiler = 'C:/Qt/Tools/mingw1310_64/bin/g++.exe'
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;$qtRoot/bin;" + $env:PATH
$outputDir = Join-Path $projectRoot 'dracoved_app/build/ashtakavarga_checks'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
Push-Location -LiteralPath $projectRoot
try {
    $includes = @("-I$qtRoot/include", "-I$qtRoot/include/QtCore", "-I$qtRoot/include/QtGui", "-I$qtRoot/include/QtWidgets")
    & $compiler -std=c++20 -O0 @includes -c dracoved_app/src/gui/vedic_panel.cpp -o "$outputDir/vedic_panel.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Vedic panel compilation failed.' }
    & $compiler -std=c++20 -O0 @includes `
        dracoved_app/src/core/ashtakavarga.cpp dracoved_app/src/core/swiss_eph.cpp dracoved_app/src/core/formatting.cpp dracoved_app/src/core/timezone_utils.cpp `
        dracoved_app/src/gui/ashtakavarga_panel.cpp dracoved_app/src/gui/ashtakavarga_tests.cpp "-L$qtRoot/lib" -lQt6Widgets -lQt6Gui -lQt6Core -o "$outputDir/checks.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Ashtakavarga compilation failed.' }
    $previousPlatform = $env:QT_QPA_PLATFORM
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        & "$outputDir/checks.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Ashtakavarga check failed.' }
    } finally { $env:QT_QPA_PLATFORM = $previousPlatform }
} finally { Pop-Location }
