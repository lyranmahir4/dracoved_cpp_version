# Standalone calculation/UI check; no application build or packaging.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qt = 'C:/Qt/6.10.1/mingw_64'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qt\bin;" + $env:PATH
$out = Join-Path $root 'dracoved_app/build/moorthi_checks'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $root
try {
    & 'C:/Qt/Tools/mingw1310_64/bin/g++.exe' -std=c++20 -O0 "-I$qt/include" "-I$qt/include/QtCore" "-I$qt/include/QtGui" "-I$qt/include/QtWidgets" `
        dracoved_app/src/core/moorthi.cpp dracoved_app/src/core/swiss_eph.cpp dracoved_app/src/core/formatting.cpp dracoved_app/src/core/timezone_utils.cpp `
        dracoved_app/src/gui/moorthi_panel.cpp dracoved_app/src/gui/moorthi_tests.cpp "-L$qt/lib" -lQt6Widgets -lQt6Gui -lQt6Core -o "$out/moorthi_checks.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Moorthi check compilation failed.' }
    $previous = $env:QT_QPA_PLATFORM
    try { $env:QT_QPA_PLATFORM = 'offscreen'; & "$out/moorthi_checks.exe"; if ($LASTEXITCODE -ne 0) { throw 'Moorthi checks failed.' } }
    finally { $env:QT_QPA_PLATFORM = $previous }
} finally { Pop-Location }
