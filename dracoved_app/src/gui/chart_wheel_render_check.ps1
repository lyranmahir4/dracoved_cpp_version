# Standalone chart-wheel render check; never builds/packages the application executable.
# Recompiles the wheel plus a small harness, reuses the other objects from the last
# normal build, and writes PNGs to dracoved_app/build/chart_wheel_checks for review.
param([string]$OutDir = 'dracoved_app/build/chart_wheel_checks')
$ErrorActionPreference = 'Stop'
# Compiler warnings arrive on stderr, which Windows PowerShell 5.1 turns into a
# terminating error. Run native tools with 'Continue' and judge by exit code.
function Invoke-Native([string]$Exe, [object[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
}
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$qtRoot = 'C:\Qt\6.10.1\mingw_64'
$compiler = 'C:\Qt\Tools\mingw1310_64\bin\g++.exe'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qtRoot\bin;" + $env:PATH
Push-Location -LiteralPath $projectRoot
try {
    $objDir = Join-Path $projectRoot 'dracoved_app/build/chart_wheel_checks/obj'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    $ninja = Get-Content dracoved_app/build/build.ninja
    $includes = (($ninja | Select-String '^  INCLUDES = ' | Select-Object -First 1).Line -replace '^  INCLUDES = ', '') -split ' '
    $defines = (($ninja | Select-String '^  DEFINES = ' | Select-Object -First 1).Line -replace '^  DEFINES = ', '') -split ' '
    $sources = @('dracoved_app/src/gui/chart_wheel_widget.cpp', 'dracoved_app/src/gui/chart_wheel_render_tests.cpp',
        'dracoved_app/build/dracoved_app_autogen/mocs_compilation.cpp')
    $fresh = @()
    foreach ($source in $sources) {
        $object = Join-Path $objDir ([IO.Path]::GetFileName($source) + '.obj')
        Invoke-Native $compiler (@('-std=gnu++20', '-O1') + $defines + $includes + @('-c', $source, '-o', $object))
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
        $fresh += $object
    }
    # The harness has its own main(); MainWindow objects are linked but never constructed.
    $replaced = @($sources | ForEach-Object { [IO.Path]::GetFileName($_) + '.obj' }) + @('main.cpp.obj')
    $old = @(Get-ChildItem dracoved_app/build/CMakeFiles/dracoved_app.dir -Recurse -Filter '*.obj' |
        Where-Object { $_.Name -notin $replaced } | ForEach-Object { $_.FullName })
    $exe = Join-Path $projectRoot 'dracoved_app/build/chart_wheel_checks/chart_wheel_checks.exe'
    Invoke-Native $compiler ($fresh + $old + @("-L$qtRoot/lib", '-lQt6Network', '-lQt6SvgWidgets', '-lQt6Widgets',
        '-lQt6Svg', '-lQt6Gui', '-lQt6Core', '-lws2_32', '-o', $exe))
    if ($LASTEXITCODE -ne 0) { throw 'Chart wheel check link failed.' }
    $previous = $env:QT_QPA_PLATFORM
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        Invoke-Native $exe @($OutDir)
        if ($LASTEXITCODE -ne 0) { throw 'Chart wheel render checks failed.' }
    } finally { $env:QT_QPA_PLATFORM = $previous }
} finally { Pop-Location }
