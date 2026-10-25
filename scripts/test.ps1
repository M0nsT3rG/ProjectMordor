<#
.SYNOPSIS
  Run the prototype test suites (unit, integration, network) and optionally a real-time
  end-to-end run of mordor-host + two mordor-client processes on 127.0.0.1.
  Results are copied to evidence/logs/ (no absolute paths, no IP addresses other than loopback).
.EXAMPLE
  ./scripts/test.ps1
  ./scripts/test.ps1 -E2E
#>
[CmdletBinding()]  # an unknown or misspelled parameter is an error, never silently ignored (it once wrote into evidence/logs)
param(
    [string]$Config = 'Release',
    [string]$BuildDir = 'build',
    [switch]$E2E,
    [switch]$GnsBuild,  # also run build-gns (MORDOR_WITH_GNS) suites, incl. integration/network on the GNS adapter
    [string]$GnsBuildDir = 'build-gns',
    [int]$Repeat = 1,
    # Test hooks only (FB-18-01 harness, scripts/test-repeat-capture.ps1): a stand-in for ctest, another evidence root,
    # and no nested self-checks. Never needed for a real run.
    [string]$CtestCommand = 'ctest',
    [string]$LogsDir = (Join-Path 'evidence' 'logs'),
    [switch]$NoSelfCheck
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    # FB-17-02: every run writes its own directory and never touches another run's evidence. The old
    # tests-<suite>-<date>.json names were rewritten by every later run that day (see tools/evidence/recover_dated.py).
    $started = Get-Date
    $commit = (git rev-parse HEAD).Trim()
    $dirty = [bool](git status --porcelain -- . ':(exclude)evidence')  # source changes not in the commit
    $runId = "tests-$($started.ToString('yyyy-MM-dd-HHmmss'))-$($commit.Substring(0, 7))$(if ($dirty) { '-dirty' })"
    $runDir = Join-Path $LogsDir $runId
    if (Test-Path $runDir) { throw "$runDir already exists: test evidence is never overwritten" }
    New-Item -ItemType Directory $runDir | Out-Null
    $runDirFull = (Resolve-Path $runDir).Path
    $suites = [ordered]@{}  # recorded suite -> expected transport (the FB-13-03 check below)
    $ctestRuns = New-Object System.Collections.ArrayList  # FB-18-01: every ctest invocation, its exit code and its files
    # Runs ctest once and keeps what THAT invocation wrote in <runDir>/<Label>/, before the next one can replace it,
    # failed invocations included. The top-level <suite>.json files hold the latest invocation's copy.
    # FB-20-02: presence is not enough. A results file must be a non-empty JSON array of cases, each with a name and
    # a result of PASSED, FAILED or SKIPPED. Returns $null when valid, or the reason. (Suite identity cannot be checked:
    # the runner does not write its suite name.)
    # FB-21-02: every case must also carry the suite it was run as (runner --suite, set by CMake to the ctest name),
    # so a file filed under the wrong suite is invalid. Old evidence without the field is read by the report, never here.
    function Test-ResultsFile([string]$Path, [string]$Suite) {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        if (-not $raw -or -not $raw.Trim().StartsWith('[')) { return 'not a JSON array' }
        # Two steps on purpose: in PowerShell 5.1, @($raw | ConvertFrom-Json) wraps the whole array as ONE element.
        try { $parsed = $raw | ConvertFrom-Json } catch { return 'not valid JSON' }
        $cases = @($parsed)
        if ($cases.Count -eq 0) { return 'no test cases' }
        foreach ($c in $cases) {
            # Each case must be a JSON object: on a nested array, PowerShell's member enumeration would present the
            # inner array's name and result as if they were the case's own (Codex review).
            if ($c -isnot [System.Management.Automation.PSCustomObject]) { return 'a case that is not a JSON object' }
            if (-not ($c.name -is [string]) -or $c.result -notin 'PASSED', 'FAILED', 'SKIPPED') { return 'a case without a name or a known result' }
            if ($c.suite -ne $Suite) { return "a case of suite '$($c.suite)', expected '$Suite'" }
        }
        return $null
    }
    # FB-21-01: names of FAILED cases in a valid results file. A FAILED case fails the run even if ctest exited 0.
    function Get-FailedCases([string]$Path) {
        $parsed = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json  # assign first: see Test-ResultsFile
        return @(@($parsed) | Where-Object { $_.result -eq 'FAILED' } | ForEach-Object { $_.name })
    }
    # Returns $true only if ctest exited 0 AND every expected suite wrote valid results AND no case FAILED
    # (FB-19-02, FB-20-02, FB-21-01).
    function Invoke-Suites([string]$Dir, [string]$Label, [int]$Iteration, $Map) {
        # FB-19-02: remove the expected result files first, so whatever exists afterwards was written by THIS
        # invocation. No timestamp comparison: its precision and fast repeats made "fresh" ambiguous.
        foreach ($k in $Map) {
            $old = Join-Path $Dir "test-results/$($k[0]).json"
            Remove-Item -LiteralPath $old -ErrorAction SilentlyContinue
            # A file that cannot be removed (locked by a stuck test process, KI-40) would be taken as fresh: stop.
            if (Test-Path -LiteralPath $old) { throw "cannot remove stale results '$old'" }  # quoted: see local-paths.ps1
        }
        $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'  # a failing ctest writes to stderr (EXP-023)
        & $CtestCommand --test-dir $Dir -C $Config --output-on-failure 2>&1 | Out-Host
        $code = $LASTEXITCODE
        $ErrorActionPreference = $eap
        $iterDir = Join-Path $runDir $Label
        New-Item -ItemType Directory $iterDir | Out-Null
        $files = @(); $missing = @(); $invalid = [ordered]@{}; $failedCases = [ordered]@{}
        foreach ($k in $Map) {  # ctest result name, recorded name, transport the suite is registered for (FB-13-03)
            $src = Join-Path $Dir "test-results/$($k[0]).json"
            if (Test-Path -LiteralPath $src) {
                $bad = Test-ResultsFile $src $k[0]
                if ($bad) {
                    # Kept for the record, but never shown as this run's latest result for the suite; an earlier
                    # invocation's valid copy is no longer the latest either (Codex review of PR #21 work).
                    Copy-Item -LiteralPath $src (Join-Path $iterDir "$($k[1]).invalid.json")
                    Remove-Item -LiteralPath (Join-Path $runDir "$($k[1]).json") -ErrorAction SilentlyContinue
                    $invalid[$k[1]] = $bad
                    Write-Host "INVALID RESULTS: $($k[1]) in ${Label}: $bad"
                    continue
                }
                Copy-Item -LiteralPath $src (Join-Path $iterDir "$($k[1]).json")
                Copy-Item -LiteralPath $src (Join-Path $runDir "$($k[1]).json") -Force  # latest invocation, for the report
                $suites[$k[1]] = $k[2]
                $files += "$Label/$($k[1]).json"
                $fc = @(Get-FailedCases $src)  # @(): a one-element return is unrolled to a scalar
                if ($fc.Count) {  # a genuine test failure: valid evidence, reported apart from invalid files
                    $failedCases[$k[1]] = $fc
                    Write-Host "FAILED CASES: $($k[1]) in ${Label}: $($fc -join ', ')"
                }
            } else {
                $missing += $k[1]  # exit code 0 does not prove a suite wrote its results
                Remove-Item -LiteralPath (Join-Path $runDir "$($k[1]).json") -ErrorAction SilentlyContinue  # not the latest any more
                Write-Host "MISSING RESULTS: $($k[1]) wrote no $($k[0]).json in $Label"
            }
        }
        [void]$ctestRuns.Add([ordered]@{ build = (Hide-LocalPaths $Dir $root); iteration = $Iteration; dir = $Label; exit_code = $code; files = $files
                                         missing = $missing; invalid = $invalid; failed_cases = $failedCases })
        Write-RunJson 'incomplete'  # on disk now: a run that stops later (E2E throw, Ctrl+C) keeps this history
        return ($code -eq 0 -and $missing.Count -eq 0 -and $invalid.Count -eq 0 -and $failedCases.Count -eq 0)
    }
    . (Join-Path $PSScriptRoot 'local-paths.ps1')  # Hide-LocalPaths: no absolute local path reaches run.json
    function Write-RunJson([string]$Result, $E2EExit = $null, [string]$Reason = $null) {
        $run = [ordered]@{
            run_id = $runId; started_utc = $started.ToUniversalTime().ToString('o'); commit = $commit; dirty = $dirty
            config = $Config; builds = @(@($BuildDir) + $(if ($GnsBuild) { @($GnsBuildDir) } else { @() }) | ForEach-Object { Hide-LocalPaths $_ $root }); repeat = $Repeat
            suites = $suites; ctest_runs = @($ctestRuns); e2e = [bool]$E2E; e2e_exit_codes = $E2EExit; result = $Result
        }
        if ($Reason) { $run.reason = Hide-LocalPaths $Reason $root }
        # UTF-8 without BOM: Python's json reader rejects a BOM.
        [IO.File]::WriteAllText((Join-Path $runDirFull 'run.json'), ($run | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding $false))
    }
    Write-RunJson 'incomplete'  # replaced at the end; a run that stops early keeps this
    $failed = $false
    $e2eExit = $null
    $udpSuites = @(@('unit', 'unit', 'udp'), @('integration', 'integration', 'udp'), @('network', 'network', 'udp'), @('transport', 'transport', 'udp'))
    for ($i = 1; $i -le $Repeat; $i++) {
        if (-not (Invoke-Suites $BuildDir "iter-$i" $i $udpSuites)) { $failed = $true }
    }
    # Optional GNS build: per-transport results are recorded separately (review #4 next steps).
    # FB-19-02: asked for and absent is a failure (missing results), never a silent skip.
    if ($GnsBuild) {
        $gnsSuites = @(@('integration-gns', 'integration-gns', 'gns'), @('network-gns', 'network-gns', 'gns'),
                       @('transport', 'transport-both', 'udp'), @('gns', 'gns', 'udp'), @('dtls', 'dtls', 'udp'))
        if (-not (Invoke-Suites $GnsBuildDir 'build-gns-iter-1' 1 $gnsSuites)) { $failed = $true }
        # FB-16-03 (KI-40): a GNS host whose start fails after GNS is initialised (192.0.2.1 is a documentation
        # address that is not on this machine, so the listen socket cannot bind) must still shut GNS down and exit.
        $tmp = Join-Path $env:TEMP "mordor-startfail-$PID"
        New-Item -ItemType Directory -Force $tmp | Out-Null
        . (Join-Path $PSScriptRoot 'wait-bounded.ps1')
        $env:MORDOR_GNS_TRACE = '1'
        $hostExe = Join-Path (Join-Path $GnsBuildDir $Config) 'mordor-host.exe'
        $smoke = if (Test-Path -LiteralPath $hostExe) { 1..3 } else { Write-Host "start-failure smoke: no $hostExe"; $failed = $true; @() }
        try {
            foreach ($i in $smoke) {
                $p = Start-Process $hostExe -ArgumentList "--transport gns --bind 192.0.2.1 --port 47831 --duration-s 1" `
                    -PassThru -WindowStyle Hidden -RedirectStandardError "$tmp/err$i.txt"
                $null = $p.Handle
                $exited = (Wait-Bounded $p 'startfail-host' 20) -eq 'exited'  # a hang is dumped (KI-40), then killed
                $shut = $exited -and (Select-String -Path "$tmp/err$i.txt" -Pattern 'shutdown conn=0 a=1' -Quiet)
                Write-Host "start-failure exit $i : exited=$exited code=$(if ($exited) { $p.ExitCode }) gns_shutdown=$shut"
                if (-not $exited -or $p.ExitCode -ne 1 -or -not $shut) { $failed = $true }
            }
        } finally {
            Remove-Item Env:MORDOR_GNS_TRACE -ErrorAction SilentlyContinue  # never leak into later steps (EXP-025)
            Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        }
    }
    # FB-13-03: every recorded suite must have run on the transport it is registered for (no leaked MORDOR_TRANSPORT).
    # Every invocation's copy is checked, not only the latest (a suite missing from a directory is skipped).
    foreach ($d in @($runDir) + @($ctestRuns | ForEach-Object { Join-Path $runDir $_.dir })) {
        python -I tools/collab/check_test_transport.py --dir $d @($suites.Keys | ForEach-Object { "$_=$($suites[$_])" })
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    if (-not $NoSelfCheck) {
        # FB-16-01: the bounded wait stays bounded even when its diagnostic never returns.
        # FB-18-01: a fail-then-pass repeat keeps both invocations and the aggregate failure.
        foreach ($check in 'test-wait-bounded.ps1', 'test-repeat-capture.ps1') {
            $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
            powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $check) 2>&1 | Out-Host
            $ErrorActionPreference = $eap
            if ($LASTEXITCODE -ne 0) { $failed = $true }
        }
    }
    if ($E2E) {
        $bin = Join-Path $BuildDir $Config
        $out = Join-Path $runDir 'e2e'  # fresh per run: no stale host_started line to match
        New-Item -ItemType Directory $out | Out-Null
        # Workaround (EXP-006, cause unknown): right after a relink, the first run of the binaries
        # exchanged no packets at all. A short warm-up run of each binary avoided it.
        & (Join-Path $bin 'mordor-host.exe') --port 47812 --duration-s 1 --log "$out/warmup-host.log"
        & (Join-Path $bin 'mordor-client.exe') --port 47812 --duration-s 1 --log "$out/warmup-client.log"
        Remove-Item "$out/warmup-*.log" -ErrorAction SilentlyContinue
        $imp = '--latency-ms 40 --jitter-ms 20 --loss 0.1'
        $h = Start-Process (Join-Path $bin 'mordor-host.exe') -ArgumentList "--port 47811 --duration-s 9 $imp --seed 5 --log $out/host.log" -PassThru -NoNewWindow
        $null = $h.Handle  # keeps ExitCode readable after exit
        # Freshly built executables can start seconds late (EXP-006); wait until the host is actually listening.
        $deadline = (Get-Date).AddSeconds(30)
        while (-not ((Test-Path "$out/host.log") -and (Select-String -Path "$out/host.log" -Pattern 'host_started' -Quiet -ErrorAction SilentlyContinue))) {
            if ((Get-Date) -gt $deadline -or $h.HasExited) { throw 'Host did not report host_started within 30 s' }
            Start-Sleep -Milliseconds 100
        }
        $c1 = Start-Process (Join-Path $bin 'mordor-client.exe') -ArgumentList "--port 47811 --duration-s 6 --pattern circle $imp --seed 6 --log $out/client1.log" -PassThru -NoNewWindow
        $c2 = Start-Process (Join-Path $bin 'mordor-client.exe') -ArgumentList "--port 47811 --duration-s 6 --pattern line $imp --seed 7 --log $out/client2.log" -PassThru -NoNewWindow
        $null = $c1.Handle; $null = $c2.Handle
        $c1.WaitForExit(); $c2.WaitForExit(); $h.WaitForExit()
        Write-Host "E2E exit codes: host=$($h.ExitCode) client1=$($c1.ExitCode) client2=$($c2.ExitCode)"
        if ($h.ExitCode -ne 0 -or $c1.ExitCode -ne 0 -or $c2.ExitCode -ne 0) { $failed = $true }
        $e2eExit = [ordered]@{ host = $h.ExitCode; client1 = $c1.ExitCode; client2 = $c2.ExitCode }
    }
    Write-RunJson $(if ($failed) { 'failed' } else { 'passed' }) $e2eExit
    $recorded = $true  # the final result is on disk: errors from here on (the failure report below) are not infrastructure
    Write-Host "results: $runDir"
    if ($failed) { Write-Error 'One or more test steps failed.' }
    Write-Host 'All test steps passed.'
} catch {
    # FB-20-01: the run stopped on an error (a stale file that cannot be removed, a missing executable, E2E start-up).
    # Keep the original reason in run.json as an infrastructure failure, then fail as before.
    if (-not $recorded -and $runDirFull -and (Get-Command Write-RunJson -ErrorAction SilentlyContinue)) {
        Write-RunJson 'infrastructure_failure' $e2eExit $_.Exception.Message
    }
    throw
} finally {
    Pop-Location
}
