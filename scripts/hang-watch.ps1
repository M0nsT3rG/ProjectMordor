# KI-40 (review #13 FB-13-02): watches this project's test executables while a long battery runs. A test process
# that has a single thread left and has used no CPU for -IdleMinutes is stuck at exit: its dump and thread stacks
# are written first (scripts/dump-stacks.ps1, local only), then it is killed and the event is logged, so the battery
# continues and the hang is evidence instead of a silent stall. Runs until -UntilFile exists.
#   ./scripts/hang-watch.ps1 -UntilFile C:\...\battery.done -Log C:\...\hangs.txt
param(
    [Parameter(Mandatory)][string]$UntilFile,
    [Parameter(Mandatory)][string]$Log,
    [double]$IdleMinutes = 3,
    [string]$OutDir = (Join-Path $env:TEMP 'mordor-dumps')
)
$ErrorActionPreference = 'Continue'
$seen = @{}
while (-not (Test-Path $UntilFile)) {
    foreach ($p in Get-Process | Where-Object { $_.ProcessName -like 'mordor_*_tests' }) {
        if ($p.Threads.Count -ne 1) { $seen.Remove($p.Id); continue }
        if (-not $seen.ContainsKey($p.Id)) { $seen[$p.Id] = @{ t = Get-Date; cpu = $p.CPU }; continue }
        $s = $seen[$p.Id]
        if ($p.CPU -ne $s.cpu) { $seen[$p.Id] = @{ t = Get-Date; cpu = $p.CPU }; continue }
        if (((Get-Date) - $s.t).TotalMinutes -lt $IdleMinutes) { continue }
        "$(Get-Date -Format o) hung-at-exit: $($p.ProcessName) pid $($p.Id) started $($p.StartTime.ToString('HH:mm:ss'))" | Out-File $Log -Append -Encoding ascii
        try { & (Join-Path $PSScriptRoot 'dump-stacks.ps1') -ProcessId $p.Id -OutDir $OutDir *>&1 | Out-File $Log -Append -Encoding ascii }
        catch { "  dump failed: $($_.Exception.Message)" | Out-File $Log -Append -Encoding ascii }
        try { Stop-Process -Id $p.Id -Force -ErrorAction Stop; "  killed" | Out-File $Log -Append -Encoding ascii }
        catch { "  stop failed: $($_.Exception.Message)" | Out-File $Log -Append -Encoding ascii }
        $seen.Remove($p.Id)
    }
    Start-Sleep -Seconds 15
}
