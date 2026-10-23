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
        ctest --test-dir $BuildDir -C $Config --output-on-failure
        if ($LASTEXITCODE -ne 0) { $failed = $true }
    }
    foreach ($suite in 'unit', 'integration', 'network') {
        $src = Join-Path $BuildDir "test-results/$suite.json"
        if (Test-Path $src) { Copy-Item $src (Join-Path $logs "tests-$suite-$stamp.json") -Force }
    }
    if ($E2E) {
        $bin = Join-Path $BuildDir $Config
        $out = Join-Path $logs "e2e-$stamp"
        New-Item -ItemType Directory -Force $out | Out-Null
        $imp = '--latency-ms 40 --jitter-ms 20 --loss 0.1'
        $h = Start-Process (Join-Path $bin 'mordor-host.exe') -ArgumentList "--port 47811 --duration-s 9 $imp --seed 5 --log $out/host.log" -PassThru -NoNewWindow
        $null = $h.Handle  # keeps ExitCode readable after exit
        # Freshly built executables can start seconds late (EXP-006); wait until the host is actually listening.
        $deadline = (Get-Date).AddSeconds(30)
        while (-not ((Test-Path "$out/host.log") -and (Select-String -Path "$out/host.log" -Pattern 'host_started' -Quiet))) {
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
