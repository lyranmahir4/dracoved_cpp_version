param(
    [Parameter(Mandatory)][string]$BuildExe,
    [Parameter(Mandatory)][string]$DistExe,
    [ValidateRange(0, 60)][int]$WaitSeconds = 15,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'
function Write-CheckMessage([string]$Message) {
    Write-Host $Message
    if ($LogFile) { Add-Content -LiteralPath $LogFile -Value $Message }
}

foreach ($target in @($BuildExe, $DistExe)) {
    $target = [IO.Path]::GetFullPath($target)
    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    $waiting = $false
    while ($true) {
        $problem = $null
        $running = @(Get-Process -Name dracoved_app -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -and [string]::Equals($_.Path, $target, [StringComparison]::OrdinalIgnoreCase)
        })
        if ($running.Count) {
            $problem = "DracoVed is running (PID: $($running.Id -join ', ')). Save your work and close its window."
        } elseif (Test-Path -LiteralPath $target) {
            $stream = $null
            try {
                # Request write access without truncating or changing any bytes.
                # Existing readers that permit writes do not prevent CMD's copy /Y.
                # FileShare.None incorrectly rejects those harmless readers.
                $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
                $stream = [IO.File]::Open($target, [IO.FileMode]::Open, [IO.FileAccess]::Write, $sharing)
            } catch {
                $problem = $_.Exception.GetBaseException().Message
            } finally {
                if ($stream) { $stream.Dispose() }
            }
        }
        if (!$problem) {
            if ($waiting) { Write-CheckMessage "Write access restored: $target" }
            break
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            Write-CheckMessage "ERROR: Cannot replace $target"
            Write-CheckMessage $problem
            Write-CheckMessage 'No process was stopped. Close the application or tool holding the file, then retry.'
            exit 1
        }
        if (!$waiting) {
            Write-CheckMessage "Waiting up to $WaitSeconds seconds for write access: $target"
            Write-CheckMessage $problem
            $waiting = $true
        }
        Start-Sleep -Milliseconds 500
    }
}
exit 0
