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
    # A mode.txt in the build dir changes it: 'skip-transport' writes no transport.json and still exits 0;
    # 'empty-unit' writes an empty unit.json ([]) and exits 0. A build dir without test-results/ gets nothing.
    $fake = Join-Path $tmp 'fake-ctest.ps1'
    Set-Content $fake -Encoding ascii -Value @'
$dir = $args[[array]::IndexOf($args, '--test-dir') + 1]
if (-not (Test-Path (Join-Path $dir 'test-results'))) { Write-Host "fake ctest: no build in $dir"; exit 0 }
$n = 1 + [int](Get-Content (Join-Path $dir 'count.txt') -ErrorAction SilentlyContinue)
Set-Content (Join-Path $dir 'count.txt') $n
$mode = Get-Content (Join-Path $dir 'mode.txt') -ErrorAction SilentlyContinue
if ($mode -eq 'empty-unit' -or ($mode -eq 'invalid-second' -and $n -ge 2)) { $emptyUnit = $true }
if ($mode -in 'invalid-second', 'swap-integration', 'failed-exit0') { $n = 2 }  # these exit 0; only the content differs
foreach ($s in 'unit', 'integration', 'network', 'transport') {
    if ($mode -eq 'skip-transport' -and $s -eq 'transport') { continue }
    if ($emptyUnit -and $s -eq 'unit') { Set-Content (Join-Path $dir "test-results/$s.json") -Encoding ascii -Value '[]'; continue }
    if ($mode -eq 'nested-unit' -and $s -eq 'unit') {
        Set-Content (Join-Path $dir "test-results/$s.json") -Encoding ascii -Value '[[{"suite":"unit","name":"x","result":"PASSED","transport":"udp"}]]'; continue }
    $res = if ($s -eq 'unit' -and $n -eq 1 -and $mode -ne 'skip-transport') { 'FAILED' } else { 'PASSED' }
    # Two cases per suite, like a real runner: a validator that only works on one-case files must fail here.
    $id = if ($mode -eq 'swap-integration' -and $s -eq 'integration') { 'unit' } else { $s }
    if ($mode -eq 'failed-exit0' -and $s -eq 'unit') { $res = 'FAILED' }
    Set-Content (Join-Path $dir "test-results/$s.json") -Encoding ascii -Value "[{`"suite`":`"$id`",`"name`":`"$s-case`",`"result`":`"$res`",`"transport`":`"udp`",`"ms`":1,`"error`":`"`"},{`"suite`":`"$id`",`"name`":`"$s-other`",`"result`":`"PASSED`",`"transport`":`"udp`",`"ms`":1,`"error`":`"`"}]"
}
Write-Host "fake ctest invocation $n ($mode)"
exit $(if ($n -eq 1 -and $mode -notin 'skip-transport', 'nested-unit' -and -not $emptyUnit) { 1 } else { 0 })
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
        # FB-20-01: stopped by an error, so an infrastructure failure with the reason; the history is still there.
        Check ($run.result -eq 'infrastructure_failure' -and $run.reason) "and run.json says infrastructure_failure with a reason ($($run.result): $($run.reason))"
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

    # FB-20-02: a results file that is present but not valid (here: an empty array) is recorded as invalid, kept
    # apart, never shown as the suite's latest result, and fails the run although ctest exited 0.
    $logs6 = Join-Path $tmp 'logs6'
    New-Item -ItemType Directory $logs6 | Out-Null
    Remove-Item (Join-Path $build 'count.txt')
    Set-Content (Join-Path $build 'mode.txt') 'empty-unit'
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs6 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    Remove-Item (Join-Path $build 'mode.txt')
    $runs = @(Get-ChildItem $logs6 -Directory)
    Check ($threw -and $runs.Count -eq 1) 'an invalid results file fails the run'
    if ($runs.Count -eq 1) {
        $d6 = $runs[0].FullName
        $run = Get-Content (Join-Path $d6 'run.json') -Raw | ConvertFrom-Json
        Check ($run.result -eq 'failed' -and $run.ctest_runs[0].invalid.unit -eq 'no test cases' -and @($run.ctest_runs[0].missing).Count -eq 0) "recorded as invalid, not missing ($($run.ctest_runs[0].invalid.unit))"
        Check ((Test-Path (Join-Path $d6 'iter-1/unit.invalid.json')) -and -not (Test-Path (Join-Path $d6 'unit.json'))) 'kept apart and not shown as the latest result'
    }

    # Codex review: valid on invocation 1, invalid on invocation 2 -> the top-level copy is not invocation 1's.
    $logs9 = Join-Path $tmp 'logs9'
    New-Item -ItemType Directory $logs9 | Out-Null
    Remove-Item (Join-Path $build 'count.txt')
    Set-Content (Join-Path $build 'mode.txt') 'invalid-second'
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs9 -Repeat 2 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    Remove-Item (Join-Path $build 'mode.txt')
    $runs = @(Get-ChildItem $logs9 -Directory)
    Check ($threw -and $runs.Count -eq 1) 'valid then invalid: the run fails'
    if ($runs.Count -eq 1) {
        $d9 = $runs[0].FullName
        Check ((Test-Path (Join-Path $d9 'iter-1/unit.json')) -and (Test-Path (Join-Path $d9 'iter-2/unit.invalid.json'))) 'both invocations are kept'
        Check (-not (Test-Path (Join-Path $d9 'unit.json'))) 'and the top level no longer shows invocation 1 as the latest'
    }

    # Codex review: a nested array is not a list of cases, even though member enumeration makes it look like one.
    $logs10 = Join-Path $tmp 'logs10'
    New-Item -ItemType Directory $logs10 | Out-Null
    Remove-Item (Join-Path $build 'count.txt') -ErrorAction SilentlyContinue
    Set-Content (Join-Path $build 'mode.txt') 'nested-unit'
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs10 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    Remove-Item (Join-Path $build 'mode.txt')
    $runs = @(Get-ChildItem $logs10 -Directory)
    if ($runs.Count -eq 1) {
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        Check ($threw -and $run.ctest_runs[0].invalid.unit -eq 'a case that is not a JSON object') "a nested array is invalid ($($run.ctest_runs[0].invalid.unit))"
        Check (-not (Test-Path (Join-Path $runs[0].FullName 'unit.json'))) 'and is never published as the latest result'
    } else { Check $false "nested-array run directory ($($runs.Count))" }

    # FB-21-01: a well-formed file with a FAILED case fails the run even when ctest exits 0, and the case is named,
    # apart from invalid files. FB-21-02: a valid file filed under another suite is invalid.
    foreach ($m in @(@('failed-exit0', 'logs11'), @('swap-integration', 'logs12'))) {
        $lg = Join-Path $tmp $m[1]
        New-Item -ItemType Directory $lg | Out-Null
        Remove-Item (Join-Path $build 'count.txt') -ErrorAction SilentlyContinue
        Set-Content (Join-Path $build 'mode.txt') $m[0]
        $threw = $false
        try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $lg -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
        catch { $threw = $true }
        Remove-Item (Join-Path $build 'mode.txt')
        $runs = @(Get-ChildItem $lg -Directory)
        if ($runs.Count -ne 1) { Check $false "$($m[0]) run directory ($($runs.Count))"; continue }
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        $c0 = $run.ctest_runs[0]
        if ($m[0] -eq 'failed-exit0') {
            Check ($threw -and $run.result -eq 'failed' -and $c0.exit_code -eq 0) "FAILED cases with ctest exit 0 fail the run ($($run.result), exit $($c0.exit_code))"
            Check ((@($c0.failed_cases.unit) -join ',') -eq 'unit-case' -and @($c0.invalid.PSObject.Properties).Count -eq 0) "the failed case is named, not marked invalid ($(@($c0.failed_cases.unit) -join ','))"
            Check (Test-Path (Join-Path $runs[0].FullName 'unit.json')) 'and the file stays valid evidence (it is the latest result)'
        } else {
            Check ($threw -and $run.result -eq 'failed' -and $c0.invalid.integration -match "suite 'unit', expected 'integration'") "a file filed under another suite is invalid ($($c0.invalid.integration))"
        }
    }

    # FB-20-01: a stale results file that cannot be removed (held open, as by a stuck test process) stops the run,
    # and run.json says why: an infrastructure failure, not just "incomplete".
    $logs7 = Join-Path $tmp 'logs7'
    New-Item -ItemType Directory $logs7 | Out-Null
    $locked = Join-Path $build 'test-results/unit.json'
    Set-Content $locked -Encoding ascii -Value '[{"name":"stale","result":"PASSED"}]'
    $hold = [IO.File]::Open($locked, 'Open', 'Read', 'None')  # no sharing: delete fails
    $threw = $false
    try { & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs7 -CtestCommand $fake -NoSelfCheck *>&1 | Out-Null }
    catch { $threw = $true }
    finally { $hold.Close() }
    $runs = @(Get-ChildItem $logs7 -Directory)
    Check ($threw -and $runs.Count -eq 1) 'an undeletable stale file stops the run'
    if ($runs.Count -eq 1) {
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        Check ($run.result -eq 'infrastructure_failure' -and $run.reason -match 'cannot remove stale') "run.json: infrastructure_failure with the reason ($($run.result): $($run.reason))"
        $text = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw
        Check (-not ($text -match '[A-Za-z]:\\\\' -or $text -match 'Users')) 'and run.json holds no absolute local path (%TEMP% placeholder)'
        Check ($run.reason -match '%TEMP%') "the reason uses the placeholder ($($run.reason))"
    }

    # FB-20-01: -GnsBuild without a GNS build: every GNS suite is missing, the start-failure smoke is skipped, and the
    # run fails with a final result (not left incomplete by a throw).
    $logs8 = Join-Path $tmp 'logs8'
    New-Item -ItemType Directory $logs8 | Out-Null
    Set-Content (Join-Path $build 'count.txt') 1
    $threw = $false
    $out = ''
    try { $out = & (Join-Path $PSScriptRoot 'test.ps1') -BuildDir $build -LogsDir $logs8 -CtestCommand $fake -NoSelfCheck -GnsBuild -GnsBuildDir (Join-Path $tmp 'no-gns-build') *>&1 | Out-String }
    catch { $threw = $true }
    $runs = @(Get-ChildItem $logs8 -Directory)
    Check ($threw -and $runs.Count -eq 1) 'an absent GNS build fails the run'
    if ($runs.Count -eq 1) {
        $run = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw | ConvertFrom-Json
        $gns = @($run.ctest_runs | Where-Object { $_.dir -eq 'build-gns-iter-1' })
        Check ($run.result -eq 'failed' -and $gns.Count -eq 1 -and @($gns[0].missing).Count -eq 5) "with result failed and all 5 GNS suites missing ($($run.result), $(@($gns[0].missing).Count))"
        # Codex review: the whole serialized record, per-invocation build paths included, holds no absolute path.
        $text = Get-Content (Join-Path $runs[0].FullName 'run.json') -Raw
        # On the JSON text, where one backslash is written as two: a drive path is X: then \\, and a UNC value
        # starts with a quote then \\\\ (a value like %TEMP%\\x has letters before its backslashes).
        Check (-not ($text -match '[A-Za-z]:\\\\' -or $text -match '"\\\\\\\\')) "run.json holds no absolute path anywhere (GNS build at an absolute path)"
        Check ($gns[0].build -like '%TEMP%*') "the GNS invocation's build is a placeholder path ($($gns[0].build))"
    }

    # Codex review of PR #20: absolute paths outside the repository, %TEMP% and the profile are hidden too.
    . (Join-Path $PSScriptRoot 'local-paths.ps1')
    $cases = @(
        @('cannot remove stale results D:\external-build\test-results\unit.json', 'cannot remove stale results <abs-path>'),
        @('no host at \\buildbox\share\Release\mordor-host.exe here', 'no host at <abs-path> here'),
        @("stale $env:TEMP\x\unit.json", 'stale %TEMP%\x\unit.json'),
        @('see https://example.com/a and 3:4 ratio', 'see https://example.com/a and 3:4 ratio'),
        @('E:/forward/slash.json', '<abs-path>'),
        # FB-21-03: spaces, non-ASCII (built with [char]: PowerShell 5.1 reads a BOM-less script as ANSI), quoted and
        # unquoted. A UNC host name has no spaces; a share name may.
        @('cannot read D:\Outra Pasta\sub dir\unit.json now', 'cannot read <abs-path> now'),
        @("cannot remove stale results 'D:\Dados do Jogo\M$([char]0xFA)sica $([char]0xE7)$([char]0xE3)o\unit.json'", "cannot remove stale results '<abs-path>'"),
        @("the term ""D:\Pasta com espa$([char]0xE7)os\Release\mordor-host.exe"" is not recognized", 'the term "<abs-path>" is not recognized'),
        @("host \\buildbox\partilha com espa$([char]0xE7)o\x.json missing", 'host <abs-path> missing'),
        @("F:\$([char]0xC1)rvore\$([char]0xED)ndice.json", '<abs-path>'),
        @("stale '$env:TEMP\a b\u.json' left", "stale '%TEMP%\a b\u.json' left"))
    foreach ($c in $cases) {
        $got = Hide-LocalPaths $c[0] 'D:\repo'
        Check ($got -eq $c[1]) "Hide-LocalPaths: '$($c[0])' -> '$got'"
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
