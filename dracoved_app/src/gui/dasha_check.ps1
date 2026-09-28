# Focused arithmetic/ephemeris/UI checks; never builds or packages production EXEs.
param([switch]$Incremental)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:/Qt/6.10.1/mingw_64'
$compiler = 'C:/Qt/Tools/mingw1310_64/bin/g++.exe'
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;$qtRoot/bin;" + $env:PATH
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
$outputDir = Join-Path $projectRoot 'dracoved_app/build/dasha_checks'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
Push-Location -LiteralPath $projectRoot
try {
    $sources = @('src/core/vimshottari.cpp', 'src/core/vedic_nakshatra.cpp', 'src/core/moorthi.cpp',
        'src/core/swiss_eph.cpp', 'src/core/formatting.cpp', 'src/core/timezone_utils.cpp',
        'src/gui/dasha_panel.cpp', 'src/gui/dasha_tests.cpp')
    $headers = @(Get-ChildItem dracoved_app/src/core -Filter '*.h') + @(Get-ChildItem dracoved_app/src/gui -Filter '*.h')
    $headerHash = @($headers | Sort-Object FullName | Get-FileHash | ForEach-Object { $_.Hash }) -join ''
    $objects = @()
    foreach ($source in $sources) {
        $path = Join-Path 'dracoved_app' $source
        $object = Join-Path $outputDir ([IO.Path]::GetFileName($path) + '.obj')
        $signature = (Get-FileHash -LiteralPath $path).Hash + $headerHash
        if (!$Incremental -or !(Test-Path -LiteralPath $object) -or !(Test-Path -LiteralPath ($object + '.inputs')) -or
            [IO.File]::ReadAllText($object + '.inputs') -ne $signature) {
            Write-Output ('Checking ' + $source)
            & $compiler -std=c++20 -O0 "-I$qtRoot/include" "-I$qtRoot/include/QtCore" "-I$qtRoot/include/QtGui" "-I$qtRoot/include/QtWidgets" -c $path -o $object
            if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
            [IO.File]::WriteAllText($object + '.inputs', $signature)
        }
        $objects += $object
    }
    $exe = Join-Path $outputDir 'dasha_checks.exe'
    & $compiler @objects "-L$qtRoot/lib" -lQt6Widgets -lQt6Gui -lQt6Core -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'Dasha check link failed.' }
    $previous = $env:QT_QPA_PLATFORM
    try { $env:QT_QPA_PLATFORM = 'offscreen'; & $exe; if ($LASTEXITCODE -ne 0) { throw 'Dasha checks failed.' } }
    finally { $env:QT_QPA_PLATFORM = $previous }
} finally { Pop-Location }
