<#
.SYNOPSIS
  Run the prototype test suites (unit, integration, network) and optionally a real-time
  end-to-end run of mordor-host + two mordor-client processes on 127.0.0.1.
  Results are copied to evidence/logs/ (no absolute paths, no IP addresses other than loopback).
.EXAMPLE
  ./scripts/test.ps1
  ./scripts/test.ps1 -E2E
#>
param(
    [string]$Config = 'Release',
    [string]$BuildDir = 'build',
    [switch]$E2E,
    [switch]$GnsBuild,  # also run build-gns (MORDOR_WITH_GNS) suites, incl. integration/network on the GNS adapter
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
    function Invoke-Suites([string]$Dir, [string]$Label, [int]$Iteration, $Map) {
        $since = Get-Date
        $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'  # a failing ctest writes to stderr (EXP-023)
        & $CtestCommand --test-dir $Dir -C $Config --output-on-failure 2>&1 | Out-Host
        $code = $LASTEXITCODE
        $ErrorActionPreference = $eap
        $iterDir = Join-Path $runDir $Label
        New-Item -ItemType Directory $iterDir | Out-Null
        $files = @()
        foreach ($k in $Map) {  # ctest result name, recorded name, transport the suite is registered for (FB-13-03)
            $src = Join-Path $Dir "test-results/$($k[0]).json"
            # Only results this invocation wrote: a suite that did not run must not bring in an older file.
            if ((Test-Path $src) -and (Get-Item $src).LastWriteTime -ge $since) {
                Copy-Item $src (Join-Path $iterDir "$($k[1]).json")
                Copy-Item $src (Join-Path $runDir "$($k[1]).json") -Force  # latest invocation, for the report
                $suites[$k[1]] = $k[2]
                $files += "$Label/$($k[1]).json"
            }
        }
        [void]$ctestRuns.Add([ordered]@{ build = $Dir; iteration = $Iteration; dir = $Label; exit_code = $code; files = $files })
        return $code
    }
    function Write-RunJson([string]$Result, $E2EExit = $null) {
        $run = [ordered]@{
            run_id = $runId; started_utc = $started.ToUniversalTime().ToString('o'); commit = $commit; dirty = $dirty
            config = $Config; builds = @($BuildDir) + $(if ($GnsBuild) { @('build-gns') } else { @() }); repeat = $Repeat
            suites = $suites; ctest_runs = @($ctestRuns); e2e = [bool]$E2E; e2e_exit_codes = $E2EExit; result = $Result
        }
        # UTF-8 without BOM: Python's json reader rejects a BOM.
        [IO.File]::WriteAllText((Join-Path $runDirFull 'run.json'), ($run | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding $false))
    }
    Write-RunJson 'incomplete'  # replaced at the end; a run that stops early keeps this
    $failed = $false
    $e2eExit = $null
    $udpSuites = @(@('unit', 'unit', 'udp'), @('integration', 'integration', 'udp'), @('network', 'network', 'udp'), @('transport', 'transport', 'udp'))
    for ($i = 1; $i -le $Repeat; $i++) {
        if ((Invoke-Suites $BuildDir "iter-$i" $i $udpSuites) -ne 0) { $failed = $true }
    }
    # Optional GNS build: per-transport results are recorded separately (review #4 next steps).
    $gnsResults = Join-Path 'build-gns' 'test-results'
    if ($GnsBuild -and (Test-Path $gnsResults)) {
        $gnsSuites = @(@('integration-gns', 'integration-gns', 'gns'), @('network-gns', 'network-gns', 'gns'),
                       @('transport', 'transport-both', 'udp'), @('gns', 'gns', 'udp'), @('dtls', 'dtls', 'udp'))
        if ((Invoke-Suites 'build-gns' 'build-gns-iter-1' 1 $gnsSuites) -ne 0) { $failed = $true }
        # FB-16-03 (KI-40): a GNS host whose start fails after GNS is initialised (192.0.2.1 is a documentation
        # address that is not on this machine, so the listen socket cannot bind) must still shut GNS down and exit.
        $tmp = Join-Path $env:TEMP "mordor-startfail-$PID"
        New-Item -ItemType Directory -Force $tmp | Out-Null
        . (Join-Path $PSScriptRoot 'wait-bounded.ps1')
        $env:MORDOR_GNS_TRACE = '1'
        try {
            foreach ($i in 1..3) {
                $p = Start-Process (Join-Path "build-gns/$Config" 'mordor-host.exe') -ArgumentList "--transport gns --bind 192.0.2.1 --port 47831 --duration-s 1" `
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
    Write-Host "results: $runDir"
    if ($failed) { Write-Error 'One or more test steps failed.' }
    Write-Host 'All test steps passed.'
} finally {
    Pop-Location
}
