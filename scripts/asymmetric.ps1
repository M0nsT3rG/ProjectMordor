<#
.SYNOPSIS
  Asymmetric-impairment scenarios as SEPARATE processes (review #6 FB-6-05). GNS fake impairment is
  process-global, so the in-process tests cannot cut one peer and not another; separate processes can.
  Scenarios:
    uplink-outage              host and client1 clean; client2's outgoing traffic is cut for -OutageS seconds.
    host-jitter-client-outage  as above, plus latency/jitter on the host process only.
  Gates: tools/e2e/check_asymmetric.py. Evidence: evidence/logs/asym-<ts>-<scenario>-<transport>/ (never reused).
.EXAMPLE
  ./scripts/asymmetric.ps1 -Scenario uplink-outage -Transport gns
#>
param(
    [ValidateSet('uplink-outage', 'host-jitter-client-outage')][string]$Scenario = 'uplink-outage',
    [ValidateSet('udp', 'gns')][string]$Transport = 'gns',
    [int]$DurationS = 24,
    [double]$OutageS = 5,
    [int]$Port = 47833,
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $bin = Join-Path $(if ($Transport -eq 'gns') { 'build-gns' } else { 'build' }) $Config
    $out = Join-Path 'evidence/logs' ("asym-{0}-{1}-{2}" -f (Get-Date -Format 'yyyy-MM-dd-HHmmss'), $Scenario, $Transport)
    if (Test-Path $out) { throw "Run directory $out already exists; refusing to overwrite evidence." }
    New-Item -ItemType Directory $out | Out-Null
    $hostImp = if ($Scenario -eq 'host-jitter-client-outage') { @{ latency_ms = 20; jitter_ms = 40 } } else { @{ latency_ms = 0; jitter_ms = 0 } }
    $outageAt = [math]::Round($DurationS * 0.3)
    # Warm-up after a relink (EXP-006).
    & (Join-Path $bin 'mordor-host.exe') --port ($Port + 1) --duration-s 1 --transport $Transport --log "$out/warmup-host.log"
    & (Join-Path $bin 'mordor-client.exe') --port ($Port + 1) --duration-s 1 --transport $Transport --log "$out/warmup-client.log"
    Remove-Item "$out/warmup-*.log" -ErrorAction SilentlyContinue

    $h = Start-Process (Join-Path $bin 'mordor-host.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $Port --duration-s $($DurationS + 6) --transport $Transport --latency-ms $($hostImp.latency_ms) --jitter-ms $($hostImp.jitter_ms) --seed 201 --log $out/host.log"
    $null = $h.Handle
    $deadline = (Get-Date).AddSeconds(30)
    while (-not ((Test-Path "$out/host.log") -and (Select-String -Path "$out/host.log" -Pattern 'host_started' -Quiet -ErrorAction SilentlyContinue))) {
        if ((Get-Date) -gt $deadline -or $h.HasExited) { throw 'Host did not report host_started within 30 s' }
        Start-Sleep -Milliseconds 100
    }
    $c1 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $Port --duration-s $DurationS --pattern circle --transport $Transport --seed 202 --log $out/client1.log"
    $c2 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $Port --duration-s $DurationS --pattern line --transport $Transport --seed 203 --outage-at-s $outageAt --outage-s $OutageS --log $out/client2.log"
    $null = $c1.Handle; $null = $c2.Handle
    $c1.WaitForExit(); $c2.WaitForExit(); $h.WaitForExit()
    $run = [ordered]@{
        scenario        = $Scenario
        transport       = $Transport
        clock           = 'real-time'
        duration_s      = $DurationS
        outage_at_s     = $outageAt
        outage_s        = $OutageS
        host_impairment = $hostImp
        impairment_scope = 'per process: host only (host-jitter scenario); client2 uplink cut during the outage'
        exit_codes      = [ordered]@{ host = $h.ExitCode; client1 = $c1.ExitCode; client2 = $c2.ExitCode }
    }
    [IO.File]::WriteAllText((Join-Path (Resolve-Path $out) 'run.json'), ($run | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
    python -I tools/e2e/check_asymmetric.py $out
    if ($LASTEXITCODE -ne 0) { Write-Error "Asymmetric scenario '$Scenario' failed its gates (see $out/check.json)." }
} finally {
    Pop-Location
}
