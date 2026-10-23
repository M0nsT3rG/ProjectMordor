<#
.SYNOPSIS
  Real-time soak test: mordor-host + two mordor-client processes on 127.0.0.1 for a fixed wall-clock
  duration under an impairment profile. client2 freezes once mid-run (simulated paused process).
  Results are analyzed against fixed gates by tools/soak/analyze.py (see docs/soak-testing.md).
.EXAMPLE
  ./scripts/soak.ps1 -Profile moderate -DurationS 120
#>
param(
    [ValidateSet('lan', 'moderate', 'harsh')][string]$Profile = 'moderate',
    [int]$DurationS = 120,
    [double]$StallS = 5,
    [int]$Port = 47813,
    [string]$Config = 'Release',
    [string]$BuildDir = 'build'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $profiles = @{
        lan      = @{ latency_ms = 2;  jitter_ms = 2;  loss = 0.01; duplicate = 0.0 }
        moderate = @{ latency_ms = 40; jitter_ms = 20; loss = 0.05; duplicate = 0.02 }
        harsh    = @{ latency_ms = 80; jitter_ms = 40; loss = 0.15; duplicate = 0.05 }
    }
    $p = $profiles[$Profile]
    $imp = "--latency-ms $($p.latency_ms) --jitter-ms $($p.jitter_ms) --loss $($p.loss) --duplicate $($p.duplicate)"
    $bin = Join-Path $BuildDir $Config
    # Every run gets a fresh directory; existing runs are evidence and are never deleted or reused (FB-2-01).
    $out = Join-Path 'evidence/logs' ("soak-{0}-{1}-g3" -f (Get-Date -Format 'yyyy-MM-dd-HHmmss'), $Profile)
    if (Test-Path $out) { throw "Run directory $out already exists; refusing to overwrite evidence." }
    New-Item -ItemType Directory $out | Out-Null
    $stallAt = [math]::Round($DurationS * 0.4)

    # Warm-up (EXP-006 workaround, cause unknown).
    & (Join-Path $bin 'mordor-host.exe') --port ($Port + 1) --duration-s 1 --log "$out/warmup-host.log"
    & (Join-Path $bin 'mordor-client.exe') --port ($Port + 1) --duration-s 1 --log "$out/warmup-client.log"
    Remove-Item "$out/warmup-*.log" -ErrorAction SilentlyContinue

    $h = Start-Process (Join-Path $bin 'mordor-host.exe') -PassThru -NoNewWindow `
        -ArgumentList "--port $Port --duration-s $($DurationS + 6) $imp --seed 101 --log $out/host.log"
    $null = $h.Handle
    $deadline = (Get-Date).AddSeconds(30)
    while (-not ((Test-Path "$out/host.log") -and (Select-String -Path "$out/host.log" -Pattern 'host_started' -Quiet -ErrorAction SilentlyContinue))) {
        if ((Get-Date) -gt $deadline -or $h.HasExited) { throw 'Host did not report host_started within 30 s' }
        Start-Sleep -Milliseconds 100
    }
    $started = (Get-Date).ToUniversalTime().ToString('o')
    $c1 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow `
        -ArgumentList "--port $Port --duration-s $DurationS --pattern circle $imp --seed 102 --log $out/client1.log"
    $c2 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow `
        -ArgumentList "--port $Port --duration-s $DurationS --pattern line $imp --seed 103 --stall-at-s $stallAt --stall-s $StallS --log $out/client2.log"
    $null = $c1.Handle; $null = $c2.Handle
    $c1.WaitForExit(); $c2.WaitForExit(); $h.WaitForExit()

    $run = [ordered]@{
        profile          = $Profile
        clock            = 'real-time'
        started_utc      = $started
        duration_s       = $DurationS
        impairment       = $p
        impairment_scope = 'every sender (host and both clients), independent seeds'
        scenario         = 'two-clients-one-stall'  # must match tools/soak/analyze.py SCENARIOS
        expected_clients = @('client1', 'client2')
        stalled_clients  = @('client2')
        gate_version     = 3
        stall_at_s       = $stallAt
        stall_s          = $StallS
        host_timeout_ms  = 3000
        exit_codes       = [ordered]@{ host = $h.ExitCode; client1 = $c1.ExitCode; client2 = $c2.ExitCode }
    }
    # UTF-8 without BOM: Python's json reader rejects a BOM.
    [IO.File]::WriteAllText((Join-Path (Resolve-Path $out) 'run.json'), ($run | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
    python -I tools/soak/analyze.py $out
    if ($LASTEXITCODE -ne 0) { Write-Error "Soak '$Profile' failed its gates (see $out/summary.json)." }
} finally {
    Pop-Location
}
