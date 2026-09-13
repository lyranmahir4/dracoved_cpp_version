# Isolated file-lock checks. Does not build, launch or overwrite DracoVed.
$ErrorActionPreference = 'Stop'
$checker = Join-Path $PSScriptRoot 'check_build_files.ps1'
$buildRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'dracoved_app/build'))
$testDir = Join-Path $buildRoot ('preflight_checks_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testDir | Out-Null
$buildFile = Join-Path $testDir 'build.exe'
$distFile = Join-Path $testDir 'dist.exe'
$logFile = Join-Path $testDir 'check.log'
$reader = $null
$child = $null
function Require([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Check([int]$ExpectedExit) {
    $output = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $checker -BuildExe $buildFile -DistExe $distFile -WaitSeconds 0 -LogFile $logFile 2>&1)
    Require ($LASTEXITCODE -eq $ExpectedExit) "Unexpected check exit code. Output: $output"
}
try {
    Check 0 # First build: neither executable exists yet.
    [IO.File]::WriteAllText($buildFile, 'new executable')
    [IO.File]::WriteAllText($distFile, 'old executable')
    Check 0
    Require ([IO.File]::ReadAllText($distFile) -eq 'old executable') 'Check changed file contents.'

    # Reproduce the false failure: a reader allows replacement, but an exclusive probe rejects it.
    $reader = [IO.File]::Open($distFile, 'Open', 'Read', ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $exclusiveRejected = $false
    try { $probe = [IO.File]::Open($distFile, 'Open', 'Write', 'None'); $probe.Dispose() }
    catch { $exclusiveRejected = $true }
    Require $exclusiveRejected 'Fixture did not reproduce the old exclusive-access failure.'
    Check 0
    & cmd.exe /d /c "copy /Y `"$buildFile`" `"$distFile`"" | Out-Null
    Require ($LASTEXITCODE -eq 0) 'Actual CMD copy rejected the compatible reader.'
    $reader.Dispose(); $reader = $null
    Require ([IO.File]::ReadAllText($distFile) -eq 'new executable') 'CMD copy did not update the test file.'

    # A genuine write blocker must still fail without modifying the file.
    $reader = [IO.File]::Open($distFile, 'Open', 'Read', 'Read')
    Check 1
    Require ((Get-Content -LiteralPath $logFile -Raw).Contains('ERROR: Cannot replace')) 'Lock details were not logged.'

    # Release that blocker while the check waits; it should recover without restarting the build.
    $outputFile = Join-Path $testDir 'retry-output.txt'
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$checker`" -BuildExe `"$buildFile`" -DistExe `"$distFile`" -WaitSeconds 10"
    $child = Start-Process -FilePath powershell.exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $outputFile
    $deadline = [DateTime]::UtcNow.AddSeconds(7)
    $sawWaiting = $false
    while ([DateTime]::UtcNow -lt $deadline -and !$child.HasExited) {
        if ((Test-Path -LiteralPath $outputFile) -and (Get-Content -LiteralPath $outputFile -Raw) -match 'Waiting up to') {
            $sawWaiting = $true; break
        }
        Start-Sleep -Milliseconds 100
    }
    Require $sawWaiting 'Retry did not report its blocker.'
    $reader.Dispose(); $reader = $null
    Require ($child.WaitForExit(10000)) 'Retry did not finish after the lock was released.'
    Require ($child.ExitCode -eq 0) 'Retry did not recover after the lock was released.'
    Require ((Get-Content -LiteralPath $outputFile -Raw).Contains('Write access restored')) 'Recovery was not reported.'
    Require ([IO.File]::ReadAllText($distFile) -eq 'new executable') 'Probing changed the file contents.'
    $child.Dispose(); $child = $null

    # An idle, hidden CMD copy stands in for a running image; never launch the real application.
    $runningFile = Join-Path $testDir 'dracoved_app.exe'
    [IO.File]::Copy($env:ComSpec, $runningFile)
    $child = Start-Process -FilePath $runningFile -ArgumentList '/d /q /k' -WindowStyle Hidden -PassThru
    $distFile = $runningFile
    Check 1
    Require ((Get-Content -LiteralPath $logFile -Raw).Contains("PID: $($child.Id)")) 'Running image was not identified by PID.'
    Write-Host 'PASS: missing/unlocked files, compatible reader and CMD copy, write blocker, running image/PID, logging, retry recovery, unchanged file contents.'
} finally {
    if ($reader) { $reader.Dispose() }
    if ($child -and !$child.HasExited) { $child.Kill(); $child.WaitForExit() }
    if ($child) { $child.Dispose() }
    $resolvedTestDir = [IO.Path]::GetFullPath($testDir)
    if (!$resolvedTestDir.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the build test directory.'
    }
    Remove-Item -LiteralPath $resolvedTestDir -Recurse -Force
}
exit 0
