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
    [int]$Repeat = 1
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $logs = Join-Path 'evidence' 'logs'
    $stamp = Get-Date -Format 'yyyy-MM-dd'
    $failed = $false
    for ($i = 1; $i -le $Repeat; $i++) {
        $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'  # see the build-gns block below
        ctest --test-dir $BuildDir -C $Config --output-on-failure 2>&1 | Out-Host
        $ErrorActionPreference = $eap
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    foreach ($suite in 'unit', 'integration', 'network', 'transport') {
        $src = Join-Path $BuildDir "test-results/$suite.json"
        if (Test-Path $src) { Copy-Item $src (Join-Path $logs "tests-$suite-$stamp.json") -Force }
    }
    # Optional GNS build: per-transport results are recorded separately (review #4 next steps).
    $gnsResults = Join-Path 'build-gns' 'test-results'
    if ($GnsBuild -and (Test-Path $gnsResults)) {
        # A failing ctest writes to stderr, which throws under ErrorActionPreference=Stop on PowerShell 5.1 and used
        # to skip the copy below, leaving the previous run's results in evidence (EXP-023). Record failures too.
        $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        ctest --test-dir build-gns -C $Config --output-on-failure 2>&1 | Out-Host
        $ErrorActionPreference = $eap
        if ($LASTEXITCODE -ne 0) { $failed = $true }
        $map = @{ 'integration-gns' = 'integration-gns'; 'network-gns' = 'network-gns'; 'transport' = 'transport-both'; 'gns' = 'gns'; 'dtls' = 'dtls' }
        foreach ($k in $map.Keys) {
            $src = Join-Path $gnsResults "$k.json"
            if (Test-Path $src) { Copy-Item $src (Join-Path $logs "tests-$($map[$k])-$stamp.json") -Force }
        }
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
    python -I tools/collab/check_test_transport.py --date $stamp unit=udp integration=udp network=udp transport=udp `
        integration-gns=gns network-gns=gns transport-both=udp gns=udp dtls=udp
    if ($LASTEXITCODE -ne 0) { $failed = $true }
    # FB-16-01: the bounded wait stays bounded even when its diagnostic never returns.
    $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test-wait-bounded.ps1') 2>&1 | Out-Host
    $ErrorActionPreference = $eap
    if ($LASTEXITCODE -ne 0) { $failed = $true }
    if ($E2E) {
        $bin = Join-Path $BuildDir $Config
        $out = Join-Path $logs "e2e-$stamp"
        New-Item -ItemType Directory -Force $out | Out-Null
        # Remove the previous run's logs; otherwise the wait below matches a stale host_started line.
        Remove-Item "$out/*.log" -ErrorAction SilentlyContinue
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
    }
    if ($failed) { Write-Error 'One or more test steps failed.' }
    Write-Host 'All test steps passed.'
} finally {
    Pop-Location
}
