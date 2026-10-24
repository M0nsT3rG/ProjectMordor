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
    [string]$BuildDir = '',
    [ValidateSet('udp', 'gns')][string]$Transport = 'udp',
    [switch]$Secure,  # review #8 FB-8-03: all three processes over DTLS (keys under %TEMP%, deleted afterwards)
    [int]$EventIntervalMs = 500,  # review #9 FB-9-02: reliable game events host -> players (0 = off)
    [int]$SeedBase = 100  # review #11 FB-11-02: UDP impairment seeds are SeedBase+1 (host), +2, +3 (clients)
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
    if (-not $BuildDir) { $BuildDir = if ($Transport -eq 'gns' -or $Secure) { 'build-gns' } else { 'build' } }
    $imp = "--transport $Transport --latency-ms $($p.latency_ms) --jitter-ms $($p.jitter_ms) --loss $($p.loss) --duplicate $($p.duplicate)"
    $bin = Join-Path $BuildDir $Config
    # Every run gets a fresh directory; existing runs are evidence and are never deleted or reused (FB-2-01).
    $mode = if ($Secure) { "$Transport-sec" } else { $Transport }
    $out = Join-Path 'evidence/logs' ("soak-{0}-{1}-{2}-g6" -f (Get-Date -Format 'yyyy-MM-dd-HHmmss'), $Profile, $mode)
    if (Test-Path $out) { throw "Run directory $out already exists; refusing to overwrite evidence." }
    New-Item -ItemType Directory $out | Out-Null
    $stallAt = [math]::Round($DurationS * 0.4)
    $secHost = ''; $secC1 = ''; $secC2 = ''
    if ($Secure) {
        $keys = Join-Path $env:TEMP ("mordor-secure-soak-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory $keys | Out-Null
        $kg = Join-Path $bin 'mordor-keygen.exe'
        $pub = @{}
        foreach ($n in 'host', 'client1', 'client2') {
            $lines = & $kg --new (Join-Path $keys "$n.key")
            if ($LASTEXITCODE -ne 0) { throw "keygen failed for $n" }
            $pub[$n] = (($lines | Select-String '^public: ').Line -replace '^public: ', '')
        }
        $inv = (& $kg --invite --key (Join-Path $keys 'host.key') --id "soak-$Profile" --ttl-s 3600)
        $secHost = "--secure-key $(Join-Path $keys 'host.key') --invitation $inv --allow-peer $($pub['client1']),$($pub['client2'])"
        $secC1 = "--secure-key $(Join-Path $keys 'client1.key') --invitation $inv"
        $secC2 = "--secure-key $(Join-Path $keys 'client2.key') --invitation $inv"
    }

    # Warm-up (EXP-006 workaround, cause unknown).
    & (Join-Path $bin 'mordor-host.exe') --port ($Port + 1) --duration-s 1 --transport $Transport --log "$out/warmup-host.log"
    & (Join-Path $bin 'mordor-client.exe') --port ($Port + 1) --duration-s 1 --transport $Transport --log "$out/warmup-client.log"
    Remove-Item "$out/warmup-*.log" -ErrorAction SilentlyContinue

    $h = Start-Process (Join-Path $bin 'mordor-host.exe') -PassThru -NoNewWindow `
        -ArgumentList "--port $Port --duration-s $($DurationS + 6) $imp --seed $($SeedBase + 1) --event-interval-ms $EventIntervalMs $secHost --log $out/host.log"
    $null = $h.Handle
    $deadline = (Get-Date).AddSeconds(30)
    while (-not ((Test-Path "$out/host.log") -and (Select-String -Path "$out/host.log" -Pattern 'host_started' -Quiet -ErrorAction SilentlyContinue))) {
        if ((Get-Date) -gt $deadline -or $h.HasExited) { throw 'Host did not report host_started within 30 s' }
        Start-Sleep -Milliseconds 100
    }
    $started = (Get-Date).ToUniversalTime().ToString('o')
    $c1 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow `
        -ArgumentList "--port $Port --duration-s $DurationS --pattern circle $imp --seed $($SeedBase + 2) $secC1 --log $out/client1.log"
    $c2 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow `
        -ArgumentList "--port $Port --duration-s $DurationS --pattern line $imp --seed $($SeedBase + 3) --stall-at-s $stallAt --stall-s $StallS $secC2 --log $out/client2.log"
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
        gate_version     = 6
        impairment_seeds = [ordered]@{ host = $SeedBase + 1; client1 = $SeedBase + 2; client2 = $SeedBase + 3 }  # UDP only; GNS fake loss is not seedable
        event_interval_ms = $EventIntervalMs
        transport        = $Transport
        secure           = [bool]$Secure
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
    if ($Secure -and $keys -and (Test-Path $keys)) { Remove-Item -Recurse -Force $keys }  # this run's test keys only
    Pop-Location
}
