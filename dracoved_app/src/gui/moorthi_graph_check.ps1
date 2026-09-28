# Focused graph/UI check; does not build or package the production application.
$ErrorActionPreference = 'Stop'
$graphProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qt = 'C:/Qt/6.10.1/mingw_64'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qt\bin;" + $env:PATH
$graphOutput = Join-Path $graphProjectRoot 'dracoved_app/build/moorthi_graph_checks'
New-Item -ItemType Directory -Force -Path $graphOutput | Out-Null
Push-Location -LiteralPath $graphProjectRoot
try {
    $graphSources = @(
        'dracoved_app/src/core/moorthi.cpp', 'dracoved_app/src/core/swiss_eph.cpp',
        'dracoved_app/src/core/formatting.cpp', 'dracoved_app/src/core/timezone_utils.cpp',
        'dracoved_app/src/gui/moorthi_panel.cpp', 'dracoved_app/src/gui/moorthi_graph_panel.cpp',
        'dracoved_app/src/gui/moorthi_graph_tests.cpp',
        'dracoved_app/src/gui/vedic_benchmark_panel.cpp', 'dracoved_app/src/core/vedic_benchmark.cpp',
        'dracoved_app/src/core/vedic_planet_nature.cpp',
        'dracoved_app/src/core/ashtakavarga.cpp', 'dracoved_app/src/core/vedic_nakshatra.cpp', 'dracoved_app/src/core/vimshottari.cpp'
    )
    & 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' -std=c++20 -O0 "-I$qt/include" "-I$qt/include/QtCore" "-I$qt/include/QtGui" "-I$qt/include/QtWidgets" @graphSources "-L$qt/lib" -lQt6Widgets -lQt6Gui -lQt6Core -o "$graphOutput/moorthi_graph_checks.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Moorthi graph compilation failed.' }
    $previousPlatform = $env:QT_QPA_PLATFORM
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        & "$graphOutput/moorthi_graph_checks.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Moorthi graph checks failed.' }
    } finally { $env:QT_QPA_PLATFORM = $previousPlatform }
} finally { Pop-Location }
