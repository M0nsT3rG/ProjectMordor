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
    # Stand-in ctest. It writes all four UDP suites; 'unit' FAILS on invocation 1 (exit 1) and passes later (exit 0).
    # A mode.txt in the build dir changes it: 'skip-transport' writes no transport.json and still exits 0.
    $fake = Join-Path $tmp 'fake-ctest.ps1'
    Set-Content $fake -Encoding ascii -Value @'
$dir = $args[[array]::IndexOf($args, '--test-dir') + 1]
$n = 1 + [int](Get-Content (Join-Path $dir 'count.txt') -ErrorAction SilentlyContinue)
Set-Content (Join-Path $dir 'count.txt') $n
$mode = Get-Content (Join-Path $dir 'mode.txt') -ErrorAction SilentlyContinue
foreach ($s in 'unit', 'integration', 'network', 'transport') {
    if ($mode -eq 'skip-transport' -and $s -eq 'transport') { continue }
    $res = if ($s -eq 'unit' -and $n -eq 1 -and $mode -ne 'skip-transport') { 'FAILED' } else { 'PASSED' }
    Set-Content (Join-Path $dir "test-results/$s.json") -Encoding ascii -Value "[{`"name`":`"$s-case`",`"result`":`"$res`",`"transport`":`"udp`",`"ms`":1,`"error`":`"`"}]"
}
Write-Host "fake ctest invocation $n ($mode)"
exit $(if ($n -eq 1 -and $mode -ne 'skip-transport') { 1 } else { 0 })
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
        Check ((@($run.ctest_runs | ForEach-Object { $_.files }) -join ',') -eq ((1, 2 | ForEach-Object { $i = $_; 'unit', 'integration', 'network', 'transport' | ForEach-Object { "iter-$i/$_.json" } }) -join ',')) 'each invocation lists its files'
    }

    # A run that stops after its suites (here: E2E asked for a build with no executables) keeps the invocation
    # history in run.json, marked incomplete (review of PR #19, finding 1).
    $logs2 = Join-Path $tmp 'logs2'
    New-Item -ItemType Directory $logs2 | Out-Null
    Remove-Item (Join-Path $build 'count.txt')
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs2 -CtestCommand $fake -NoSelfCheck -E2E *>&1 | Out-Null }
    catch { $threw = $true }
    $runs = @(Get-ChildItem $logs2 -Directory)
    Check ($threw -and $runs.Count -eq 1) 'a run that stops early still leaves its directory'
    if ($runs.Count -eq 1) {
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        Check ($run.result -eq 'incomplete') "and run.json says incomplete ($($run.result))"
        Check ((@($run.ctest_runs | ForEach-Object { "$($_.dir):$($_.exit_code)" }) -join ',') -eq 'iter-1:1') "with the invocation and its exit code ($(@($run.ctest_runs | ForEach-Object { "$($_.dir):$($_.exit_code)" }) -join ','))"
    }

    # FB-19-02: a suite that writes no results fails the run even when ctest exits 0, and a stale file left by an
    # earlier invocation is never taken as this invocation's result.
    $logs4 = Join-Path $tmp 'logs4'
    New-Item -ItemType Directory $logs4 | Out-Null
    Remove-Item (Join-Path $build 'count.txt')
    Set-Content (Join-Path $build 'mode.txt') 'skip-transport'
    $stale = Join-Path $build 'test-results/transport.json'
    Set-Content $stale -Encoding ascii -Value '[{"name":"stale","result":"PASSED","transport":"udp","ms":1,"error":""}]'
    (Get-Item $stale).LastWriteTime = (Get-Date).AddMinutes(5)  # newer than the run: a timestamp check would take it
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs4 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    $runs = @(Get-ChildItem $logs4 -Directory)
    Check ($threw -and $runs.Count -eq 1) 'a missing suite fails the run although ctest exited 0'
    if ($runs.Count -eq 1) {
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        Check ($run.result -eq 'failed') "run.json result is failed ($($run.result))"
        Check ((@($run.ctest_runs[0].missing) -join ',') -eq 'transport') "the missing suite is named ($(@($run.ctest_runs[0].missing) -join ','))"
        Check ($run.ctest_runs[0].exit_code -eq 0) 'next to ctest exit code 0'
        Check (-not (Test-Path (Join-Path $runs[0].FullName 'iter-1/transport.json'))) 'the stale file was not taken as a result'
        Check (-not (Test-Path $stale)) 'and it was removed before the invocation'
    }
    Remove-Item (Join-Path $build 'mode.txt')

    # Positive control (independent review): a clean invocation must PASS, so the failure checks above cannot be
    # satisfied by a harness or test.ps1 that fails every run.
    $logs5 = Join-Path $tmp 'logs5'
    New-Item -ItemType Directory $logs5 | Out-Null
    Set-Content (Join-Path $build 'count.txt') 1  # the next invocation is the passing one
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs5 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    $runs = @(Get-ChildItem $logs5 -Directory)
    Check (-not $threw -and $runs.Count -eq 1) 'a clean run passes (positive control)'
    if ($runs.Count -eq 1) {
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        Check ($run.result -eq 'passed' -and @($run.ctest_runs[0].missing).Count -eq 0 -and $run.ctest_runs[0].exit_code -eq 0) "with result passed, nothing missing, exit 0 ($($run.result))"
    }

    # A misspelled parameter is an error before anything is written (review of PR #19, finding 2).
    $logs3 = Join-Path $tmp 'logs3'
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogDir $logs3 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $_.Exception.Message -match 'LogDir' }
    Check $threw 'a misspelled parameter (-LogDir) is refused by name'
    Check (-not (Test-Path $logs3)) 'and nothing is written'
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
if ($failures) { Write-Host "repeat-capture self-check: $($failures.Count) failed"; exit 1 }
Write-Host 'repeat-capture self-check: all passed'
