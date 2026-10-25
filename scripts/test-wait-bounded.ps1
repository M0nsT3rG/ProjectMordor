# FB-16-01 self-check for wait-bounded.ps1, using only disposable processes it starts itself (never a mordor or
# user process). A stuck target plus a diagnostic that never returns (and leaves a child, as cdb would) must still
# end in bounded time, with the target, the diagnostic and its child all killed. Run by test.ps1; exit 1 on failure.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'wait-bounded.ps1')
$dir = Join-Path $env:TEMP "mordor-wb-test-$PID"
New-Item -ItemType Directory -Force $dir | Out-Null
$failures = @()
function Check([bool]$ok, [string]$what) { if ($ok) { Write-Host "[PASS] $what" } else { Write-Host "[FAIL] $what"; $script:failures += $what } }
function Sleeper() { Start-Process powershell -ArgumentList '-NoProfile', '-Command', 'Start-Sleep 3600' -WindowStyle Hidden -PassThru }
function Alive([int]$id) { [bool](Get-Process -Id $id -ErrorAction SilentlyContinue) }
try {
    $childFile = Join-Path $dir 'child.pid'
    Set-Content (Join-Path $dir 'never.ps1') -Encoding ascii -Value @"
param([int]`$ProcessId)
`$c = Start-Process powershell -ArgumentList '-NoProfile', '-Command', 'Start-Sleep 3600' -WindowStyle Hidden -PassThru
Set-Content '$childFile' `$c.Id
Start-Sleep 3600
"@
    Set-Content (Join-Path $dir 'ok.ps1') -Encoding ascii -Value 'param([int]$ProcessId) "dump of $ProcessId"; exit 0'
    Set-Content (Join-Path $dir 'fail.ps1') -Encoding ascii -Value 'param([int]$ProcessId) "no dump"; exit 1'

    $p = Start-Process powershell -ArgumentList '-NoProfile', '-Command', 'exit 0' -WindowStyle Hidden -PassThru
    Check ((Wait-Bounded $p 'quick' 30) -eq 'exited') 'a process that exits is reported exited'

    $t = Sleeper
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $r = Wait-Bounded $t 'stuck' 0 -DumpTimeoutS 10 -DumpScript (Join-Path $dir 'never.ps1')  # 10 s: room for a cold start under load
    $sw.Stop()
    Check ($r -eq 'hung_dump_timeout') "a diagnostic that never returns gives hung_dump_timeout (got $r)"
    Check ($sw.Elapsed.TotalSeconds -lt 30) "and returns in bounded time ($([int]$sw.Elapsed.TotalMilliseconds) ms for a 10 s dump limit)"
    Start-Sleep -Milliseconds 500
    Check (-not (Alive $t.Id)) 'the stuck target is killed'
    $child = if (Test-Path $childFile) { [int](Get-Content $childFile) } else { 0 }
    Check ($child -gt 0 -and -not (Alive $child)) "the diagnostic's own child is killed too (pid $child)"
    if ($child -gt 0 -and (Alive $child)) { Stop-Process -Id $child -Force }

    $t = Sleeper
    Check ((Wait-Bounded $t 'stuck' 0 -DumpTimeoutS 30 -DumpScript (Join-Path $dir 'ok.ps1')) -eq 'hung') 'a diagnostic that finishes gives hung'
    Start-Sleep -Milliseconds 500
    Check (-not (Alive $t.Id)) 'and the target is killed'

    $t = Sleeper
    Check ((Wait-Bounded $t 'stuck' 0 -DumpTimeoutS 30 -DumpScript (Join-Path $dir 'fail.ps1')) -eq 'hung') 'a diagnostic that fails gives hung'
    Start-Sleep -Milliseconds 500
    Check (-not (Alive $t.Id)) 'and the target is still killed'

    # The helper itself throwing (not the diagnostic process failing): the finally must still kill the target.
    $t = Sleeper
    function Invoke-BoundedDump { throw 'helper broke' }
    Check ((Wait-Bounded $t 'stuck' 0 2>$null 3>$null) -eq 'hung') 'a throwing dump helper gives hung'
    Start-Sleep -Milliseconds 500
    Check (-not (Alive $t.Id)) 'and the target is killed by the finally'
} finally {
    Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
}
if ($failures) { Write-Host "wait-bounded self-check: $($failures.Count) failed"; exit 1 }
Write-Host 'wait-bounded self-check: all passed'
