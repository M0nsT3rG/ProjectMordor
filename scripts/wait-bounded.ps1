# Review #15 FB-15-03/05: wait for one of our processes with a time limit. A process that does not exit in time is
# dumped (dump-stacks.ps1, local only), killed, and reported as 'hung': an infrastructure failure, kept apart from
# gate failures, instead of a battery that silently waits forever (KI-40 can hit mordor-host/mordor-client too).
# Dot-source it:  . (Join-Path $PSScriptRoot 'wait-bounded.ps1')
function Wait-Bounded([System.Diagnostics.Process]$Proc, [string]$Name, [int]$TimeoutS) {
    if ($Proc.WaitForExit($TimeoutS * 1000)) { return 'exited' }
    Write-Warning "$Name (pid $($Proc.Id)) did not exit within $TimeoutS s: dumping its stacks, then killing it"
    try { & (Join-Path $PSScriptRoot 'dump-stacks.ps1') -ProcessId $Proc.Id *>&1 | Out-Host } catch { Write-Warning "dump failed: $($_.Exception.Message)" }
    try { Stop-Process -Id $Proc.Id -Force -ErrorAction Stop } catch { Write-Warning "kill failed: $($_.Exception.Message)" }
    return 'hung'
}
