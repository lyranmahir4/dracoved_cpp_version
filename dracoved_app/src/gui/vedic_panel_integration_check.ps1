# Builds and runs the focused Vedic D1 integration check. It never builds or
# packages the production dracoved_app.exe.
param([switch]$Incremental)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:\Qt\6.10.1\mingw_64'
$compiler = 'C:\Qt\Tools\mingw1310_64\bin\g++.exe'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qtRoot\bin;" + $env:PATH
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
Push-Location -LiteralPath $projectRoot
try {
    $outputDir = Join-Path $projectRoot 'dracoved_app/build/vedic_panel_integration_checks'
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $ninja = Get-Content dracoved_app/build/build.ninja
    $includes = (($ninja | Select-String '^  INCLUDES = ' | Select-Object -First 1).Line -replace '^  INCLUDES = ', '') -split ' '
    $defines = (($ninja | Select-String '^  DEFINES = ' | Select-Object -First 1).Line -replace '^  DEFINES = ', '') -split ' '
    $sources = @(Get-ChildItem dracoved_app/src/gui -Filter 'main_window*.cpp' | ForEach-Object { $_.FullName }) + @(
        'dracoved_app/src/gui/vedic_panel.cpp',
        'dracoved_app/src/gui/south_indian_chart.cpp',
        'dracoved_app/src/gui/ashtakavarga_panel.cpp',
        'dracoved_app/src/gui/vedic_benchmark_panel.cpp',
        'dracoved_app/src/core/vedic_benchmark.cpp',
        'dracoved_app/src/core/vedic_planet_nature.cpp',
        'dracoved_app/src/core/ashtakavarga.cpp',
        'dracoved_app/src/gui/moorthi_panel.cpp',
        'dracoved_app/src/gui/moorthi_graph_panel.cpp',
        'dracoved_app/src/gui/tara_panel.cpp',
        'dracoved_app/src/gui/dasha_panel.cpp',
        'dracoved_app/src/core/vimshottari.cpp',
        'dracoved_app/src/core/vedic_nakshatra.cpp',
        'dracoved_app/src/core/moorthi.cpp',
        'dracoved_app/src/gui/vedic_panel_integration_tests.cpp',
        'dracoved_app/build/dracoved_app_autogen/mocs_compilation.cpp'
    )
    $freshObjects = @()
    $headerHashes = @(Get-ChildItem dracoved_app/src -Recurse -Filter '*.h' | Sort-Object FullName |
        Get-FileHash | ForEach-Object { $_.Hash }) -join ''
    foreach ($source in $sources) {
        $object = Join-Path $outputDir ([IO.Path]::GetFileName($source) + '.obj')
        $signature = (Get-FileHash -LiteralPath $source).Hash + $headerHashes
        if ($Incremental -and (Test-Path -LiteralPath $object) -and (Test-Path -LiteralPath ($object + '.inputs')) -and
            [IO.File]::ReadAllText($object + '.inputs') -eq $signature) {
            $freshObjects += $object
            continue
        }
        Write-Output ('Checking ' + [IO.Path]::GetFileName($source))
        & $compiler -std=gnu++20 -O0 @defines @includes -c $source -o $object
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
        [IO.File]::WriteAllText($object + '.inputs', $signature)
        $freshObjects += $object
    }
    $replacedNames = @($sources | ForEach-Object { [IO.Path]::GetFileName($_) + '.obj' }) + @('main.cpp.obj')
    $oldObjects = @(Get-ChildItem dracoved_app/build/CMakeFiles/dracoved_app.dir -Recurse -Filter '*.obj' |
        Where-Object { $_.Name -notin $replacedNames } | ForEach-Object { $_.FullName })
    $executable = Join-Path $outputDir 'vedic_panel_integration_checks.exe'
    & $compiler @freshObjects @oldObjects '-LC:\Qt\6.10.1\mingw_64\lib' -lQt6Network -lQt6SvgWidgets -lQt6Widgets -lQt6Svg -lQt6Gui -lQt6Core -lws2_32 -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Vedic integration-test link failed.' }
    $previousPlatform = $env:QT_QPA_PLATFORM
    $previousDll = $env:DRACOVED_SWE_DLL
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:DRACOVED_SWE_DLL = Join-Path $projectRoot 'swedll64.dll'
        & $executable
        if ($LASTEXITCODE -ne 0) { throw 'Vedic integration checks failed.' }
    } finally {
        $env:QT_QPA_PLATFORM = $previousPlatform
        $env:DRACOVED_SWE_DLL = $previousDll
    }
} finally {
    Pop-Location
}
