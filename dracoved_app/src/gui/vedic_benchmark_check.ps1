# Focused combined-score/graph check. Leaves production EXEs untouched.
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:/Qt/6.10.1/mingw_64'
$compiler = 'C:/Qt/Tools/mingw1310_64/bin/g++.exe'
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;$qtRoot/bin;" + $env:PATH
$outputDir = Join-Path $projectRoot 'dracoved_app/build/vedic_benchmark_checks'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
Push-Location -LiteralPath $projectRoot
try {
    $includes = @("-I$qtRoot/include", "-I$qtRoot/include/QtCore", "-I$qtRoot/include/QtGui", "-I$qtRoot/include/QtWidgets")
    & $compiler -std=c++20 -O0 @includes -c dracoved_app/src/gui/vedic_panel.cpp -o "$outputDir/vedic_panel.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Vedic panel integration compilation failed.' }
    $sources = @('src/core/ashtakavarga.cpp','src/core/vedic_benchmark.cpp','src/core/vedic_planet_nature.cpp','src/core/vedic_nakshatra.cpp',
        'src/core/moorthi.cpp','src/core/vimshottari.cpp','src/core/swiss_eph.cpp','src/core/formatting.cpp','src/core/timezone_utils.cpp',
        'src/gui/moorthi_panel.cpp','src/gui/dasha_panel.cpp',
        'src/gui/moorthi_graph_panel.cpp','src/gui/vedic_benchmark_panel.cpp','src/gui/vedic_benchmark_tests.cpp') |
        ForEach-Object { Join-Path 'dracoved_app' $_ }
    & $compiler -std=c++20 -O0 @includes @sources "-L$qtRoot/lib" -lQt6Widgets -lQt6Gui -lQt6Core -o "$outputDir/checks.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark compilation failed.' }
    $previousPlatform = $env:QT_QPA_PLATFORM
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        & "$outputDir/checks.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Benchmark check failed.' }
    } finally { $env:QT_QPA_PLATFORM = $previousPlatform }
} finally { Pop-Location }
