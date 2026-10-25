# FB-18-01 self-check for test.ps1 -Repeat: a suite that fails on the first invocation and passes on the second must
# leave BOTH invocations' results, both exit codes, and an aggregate failure. Uses a stand-in for ctest and a
# temporary evidence root, so nothing is built and nothing is written to the repository. Run by test.ps1; exit 1 on failure.
$ErrorActionPreference = 'Stop'
$tmp = Join-Path $env:TEMP "mordor-repeat-test-$PID"
$build = Join-Path $tmp 'build'
$logs = Join-Path $tmp 'logs'
New-Item -ItemType Directory -Force (Join-Path $build 'test-results'), $logs | Out-Null
$failures = @()
function Check([bool]$ok, [string]$what) { if ($ok) { Write-Host "[PASS] $what" } else { Write-Host "[FAIL] $what"; $script:failures += $what } }
try {
    # Stand-in ctest: invocation 1 records a FAILED case and exits 1; invocation 2 records PASSED and exits 0.
    $fake = Join-Path $tmp 'fake-ctest.ps1'
    Set-Content $fake -Encoding ascii -Value @'
$dir = $args[[array]::IndexOf($args, '--test-dir') + 1]
$n = 1 + [int](Get-Content (Join-Path $dir 'count.txt') -ErrorAction SilentlyContinue)
Set-Content (Join-Path $dir 'count.txt') $n
$res = if ($n -eq 1) { 'FAILED' } else { 'PASSED' }
Set-Content (Join-Path $dir 'test-results/unit.json') -Encoding ascii -Value "[{`"name`":`"flaky`",`"result`":`"$res`",`"transport`":`"udp`",`"ms`":1,`"error`":`"`"}]"
Write-Host "fake ctest invocation $n : $res"
exit $(if ($n -eq 1) { 1 } else { 0 })
'@
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs -Repeat 2 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    Check $threw 'test.ps1 reports the run as failed'
    $runs = @(Get-ChildItem $logs -Directory)
    Check ($runs.Count -eq 1) "one run directory ($($runs.Count))"
    if ($runs.Count -eq 1) {
        $d = $runs[0].FullName
        $run = Get-Content (Join-Path $d 'run.json') -Raw | ConvertFrom-Json
        Check ($run.result -eq 'failed') "run.json result is failed ($($run.result))"
        Check ((@($run.ctest_runs | ForEach-Object { $_.exit_code }) -join ',') -eq '1,0') "both exit codes recorded ($(@($run.ctest_runs | ForEach-Object { $_.exit_code }) -join ','))"
        $r1 = Get-Content (Join-Path $d 'iter-1/unit.json') -Raw -ErrorAction SilentlyContinue
        $r2 = Get-Content (Join-Path $d 'iter-2/unit.json') -Raw -ErrorAction SilentlyContinue
        Check ($r1 -match '"FAILED"') 'the failing invocation keeps its own result file'
        Check ($r2 -match '"PASSED"' -and $r2 -notmatch 'FAILED') 'the passing invocation keeps its own result file'
        Check ((Get-Content (Join-Path $d 'unit.json') -Raw) -eq $r2) 'the top-level file is the latest invocation'
        Check ((@($run.ctest_runs | ForEach-Object { $_.files }) -join ',') -eq 'iter-1/unit.json,iter-2/unit.json') 'each invocation lists its files'
    }
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
if ($failures) { Write-Host "repeat-capture self-check: $($failures.Count) failed"; exit 1 }
Write-Host 'repeat-capture self-check: all passed'
