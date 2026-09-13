# Standalone integration check; never builds/packages the application executable.
param([switch]$Incremental)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:\Qt\6.10.1\mingw_64'
$compiler = 'C:\Qt\Tools\mingw1310_64\bin\g++.exe'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qtRoot\bin;" + $env:PATH
$env:QTFRAMEWORK_BYPASS_LICENSE_CHECK = '1'
Push-Location -LiteralPath $projectRoot
try {
    $outputDir = Join-Path $projectRoot 'dracoved_app/build/lunar_return_view_checks'
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $ninja = Get-Content dracoved_app/build/build.ninja
    $includes = (($ninja | Select-String '^  INCLUDES = ' | Select-Object -First 1).Line -replace '^  INCLUDES = ', '') -split ' '
    $defines = (($ninja | Select-String '^  DEFINES = ' | Select-Object -First 1).Line -replace '^  DEFINES = ', '') -split ' '
    # MainWindow's layout is unchanged. Recompile the wheel and its allocating
    # translation unit together, plus moc metadata; reuse other normal-build objects.
    $sources = @('dracoved_app/src/gui/main_window.cpp', 'dracoved_app/src/gui/chart_wheel_widget.cpp',
        'dracoved_app/src/gui/lunar_return_view_tests.cpp', 'dracoved_app/build/dracoved_app_autogen/mocs_compilation.cpp')
    $headerHashes = @(Get-ChildItem dracoved_app/src -Recurse -Filter '*.h' | Sort-Object FullName | Get-FileHash | ForEach-Object { $_.Hash }) -join ''
    $freshObjects = @()
    foreach ($source in $sources) {
        $object = Join-Path $outputDir ([IO.Path]::GetFileName($source) + '.obj')
        $signature = (Get-FileHash -LiteralPath $source).Hash + $headerHashes
        if (!$Incremental -or !(Test-Path -LiteralPath $object) -or !(Test-Path -LiteralPath ($object + '.inputs')) -or
            [IO.File]::ReadAllText($object + '.inputs') -ne $signature) {
            Write-Output ('Checking ' + [IO.Path]::GetFileName($source))
            & $compiler -std=gnu++20 -O0 @defines @includes -c $source -o $object
            if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
            [IO.File]::WriteAllText($object + '.inputs', $signature)
        }
        $freshObjects += $object
    }
    $replacedNames = @($sources | ForEach-Object { [IO.Path]::GetFileName($_) + '.obj' }) + @('main.cpp.obj')
    $oldObjects = @(Get-ChildItem dracoved_app/build/CMakeFiles/dracoved_app.dir -Recurse -Filter '*.obj' |
        Where-Object { $_.Name -notin $replacedNames } | ForEach-Object { $_.FullName })
    $executable = Join-Path $outputDir 'lunar_return_view_checks.exe'
    & $compiler @freshObjects @oldObjects "-L$qtRoot/lib" -lQt6Network -lQt6SvgWidgets -lQt6Widgets -lQt6Svg -lQt6Gui -lQt6Core -lws2_32 -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Lunar Return test link failed.' }
    $previousPlatform = $env:QT_QPA_PLATFORM
    $previousDll = $env:DRACOVED_SWE_DLL
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:DRACOVED_SWE_DLL = Join-Path $projectRoot 'swedll64.dll'
        & $executable
        if ($LASTEXITCODE -ne 0) { throw 'Lunar Return view checks failed.' }
    } finally { $env:QT_QPA_PLATFORM = $previousPlatform; $env:DRACOVED_SWE_DLL = $previousDll }
} finally { Pop-Location }
