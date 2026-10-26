# Review #15 FB-15-03/05: wait for one of our processes with a time limit. A process that does not exit in time is
# dumped (dump-stacks.ps1, local only), killed, and reported as 'hung': an infrastructure failure, kept apart from
# gate failures, instead of a battery that silently waits forever (KI-40 can hit mordor-host/mordor-client too).
# Dot-source it:  . (Join-Path $PSScriptRoot 'wait-bounded.ps1')

# FB-16-01: the diagnostic itself is bounded. It runs in its own process (cdb symbol loading or a full dump can
# stall); on expiry that process and its children (cdb) are killed. Returns 'dumped', 'dump_failed' or 'dump_timeout'.
function Invoke-BoundedDump([int]$ProcessId, [int]$TimeoutS = 180, [string]$Script = (Join-Path $PSScriptRoot 'dump-stacks.ps1'), [string[]]$ExtraArgs = @()) {
    $out = Join-Path $env:TEMP "mordor-dump-$ProcessId-$PID.out"
    $d = Start-Process powershell -ArgumentList (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$Script`"", '-ProcessId', $ProcessId) + $ExtraArgs) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $out
    $null = $d.Handle  # keeps ExitCode readable after exit
    $result = if ($d.WaitForExit($TimeoutS * 1000)) { if ($d.ExitCode -eq 0) { 'dumped' } else { 'dump_failed' } } else {
        $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'  # taskkill writes to stderr when nothing is left
        taskkill.exe /T /F /PID $d.Id 2>&1 | Out-Null
        $ErrorActionPreference = $eap
        $d.WaitForExit(5000) | Out-Null
        'dump_timeout'
    }
    if (Test-Path $out) { Get-Content $out | Write-Host; Remove-Item $out -ErrorAction SilentlyContinue }
    Write-Host "diagnostic: $result"
    return $result
}

# Returns 'exited', 'hung' (dumped or dump failed, then killed) or 'hung_dump_timeout' (the dump did not finish in
# -DumpTimeoutS; killed anyway). The target is killed in every case, even if the diagnostic throws.
# Time bound (FB-17-01): -DumpTimeoutS (default 180 s, room for a cold symbol download) comes ON TOP of -TimeoutS,
# plus up to 5 s for the helper tree to die and the target kill. So the intended budget for one hung process is
# about TimeoutS + DumpTimeoutS + 5 s. It is a budget, not a strict guarantee (FB-18-03): starting the helper,
# taskkill, reading its output and cleanup have no deadline of their own. It bounds each wait, not a whole battery.
function Wait-Bounded([System.Diagnostics.Process]$Proc, [string]$Name, [int]$TimeoutS, [int]$DumpTimeoutS = 180,
                      [string]$DumpScript = (Join-Path $PSScriptRoot 'dump-stacks.ps1')) {
    if ($Proc.WaitForExit($TimeoutS * 1000)) { return 'exited' }
    Write-Warning "$Name (pid $($Proc.Id)) did not exit within $TimeoutS s: dumping its stacks (at most $DumpTimeoutS s), then killing it"
    $dump = 'dump_failed'
    try { $dump = Invoke-BoundedDump $Proc.Id $DumpTimeoutS $DumpScript }
    catch { Write-Warning "dump failed: $($_.Exception.Message)" }
    finally {
        try { Stop-Process -Id $Proc.Id -Force -ErrorAction Stop } catch { Write-Warning "kill failed: $($_.Exception.Message)" }
    }
    if ($dump -eq 'dump_timeout') { return 'hung_dump_timeout' }
    return 'hung'
}
