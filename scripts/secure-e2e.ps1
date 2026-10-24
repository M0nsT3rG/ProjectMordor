<#
.SYNOPSIS
  Secure-mode end-to-end test (review #8 FB-8-03): mordor-host + two mordor-clients as separate processes on
  127.0.0.1, over DTLS with pinned keys and an invitation, plus negative cases against a second secure host.
  Keys are generated into a fresh directory under %TEMP% (never the repository) and deleted at the end.
  Gates: tools/e2e/check_secure.py. Evidence: evidence/logs/secure-e2e-<ts>-<transport>/ (logs only, no keys).
.EXAMPLE
  ./scripts/secure-e2e.ps1 -Transport gns
#>
param(
    [ValidateSet('udp', 'gns')][string]$Transport = 'gns',
    [int]$DurationS = 10,
    [int]$Port = 47843,
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
$keys = Join-Path $env:TEMP ("mordor-secure-e2e-" + [guid]::NewGuid().ToString('N'))
try {
    $bin = Join-Path 'build-gns' $Config  # secure mode needs the GNS/OpenSSL build for both transports
    $kg = Join-Path $bin 'mordor-keygen.exe'
    $out = Join-Path 'evidence/logs' ("secure-e2e-{0}-{1}" -f (Get-Date -Format 'yyyy-MM-dd-HHmmss'), $Transport)
    if (Test-Path $out) { throw "Run directory $out already exists; refusing to overwrite evidence." }
    New-Item -ItemType Directory $out | Out-Null
    New-Item -ItemType Directory $keys | Out-Null
    function New-Key($name) {
        $lines = & $kg --new (Join-Path $keys "$name.key")
        if ($LASTEXITCODE -ne 0) { throw "keygen failed for $name" }
        return (($lines | Select-String '^public: ').Line -replace '^public: ', '')
    }
    $pubHost = New-Key 'host'; $pub1 = New-Key 'client1'; $pub2 = New-Key 'client2'; $null = New-Key 'stranger'
    $inv = (& $kg --invite --key (Join-Path $keys 'host.key') --id "e2e-$Transport" --ttl-s 600)
    $otherInv = (& $kg --invite --key (Join-Path $keys 'host.key') --id "other-$Transport" --ttl-s 600)
    $expiredInv = (& $kg --invite --key (Join-Path $keys 'host.key') --id "old-$Transport" --ttl-s 1)
    Start-Sleep -Seconds 2  # $expiredInv is now past its expiry
    $hostKey = Join-Path $keys 'host.key'
    $common = "--transport $Transport"

    function Wait-HostStarted($log, $proc) {
        $deadline = (Get-Date).AddSeconds(30)
        while (-not ((Test-Path $log) -and (Select-String -Path $log -Pattern 'host_started' -Quiet -ErrorAction SilentlyContinue))) {
            if ((Get-Date) -gt $deadline -or $proc.HasExited) { throw "Host did not report host_started ($log)" }
            Start-Sleep -Milliseconds 100
        }
    }
    # --- Positive: two invited players play over DTLS. ---
    $h = Start-Process (Join-Path $bin 'mordor-host.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $Port --duration-s $($DurationS + 4) $common --secure-key $hostKey --invitation $inv --allow-peer $pub1,$pub2 --log $out/host.log"
    $null = $h.Handle
    Wait-HostStarted "$out/host.log" $h
    $c1 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $Port --duration-s $DurationS --pattern circle $common --secure-key $(Join-Path $keys 'client1.key') --invitation $inv --log $out/client1.log"
    $c2 = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $Port --duration-s $DurationS --pattern line $common --secure-key $(Join-Path $keys 'client2.key') --invitation $inv --log $out/client2.log"
    $null = $c1.Handle; $null = $c2.Handle
    $c1.WaitForExit(); $c2.WaitForExit(); $h.WaitForExit()

    # --- Negatives against a second secure host (only client1 is invited). ---
    $p2 = $Port + 2
    $h2 = Start-Process (Join-Path $bin 'mordor-host.exe') -PassThru -NoNewWindow -ArgumentList `
        "--port $p2 --duration-s 14 $common --secure-key $hostKey --invitation $inv --allow-peer $pub1 --log $out/host2.log"
    $null = $h2.Handle
    Wait-HostStarted "$out/host2.log" $h2
    $neg = [ordered]@{}
    $cases = [ordered]@{
        plaintext          = "--connect-deadline-s 4"
        stranger           = "--connect-deadline-s 4 --secure-key $(Join-Path $keys 'stranger.key') --invitation $inv"
        wrong_invitation   = "--connect-deadline-s 4 --secure-key $(Join-Path $keys 'client1.key') --invitation $otherInv"
        expired_invitation = "--connect-deadline-s 4 --secure-key $(Join-Path $keys 'client1.key') --invitation $expiredInv"
        missing_key        = "--connect-deadline-s 4 --secure-key $(Join-Path $keys 'nope.key') --invitation $inv"
    }
    foreach ($name in $cases.Keys) {
        $p = Start-Process (Join-Path $bin 'mordor-client.exe') -PassThru -NoNewWindow -RedirectStandardError "$out/$name.stderr.txt" -ArgumentList `
            "--port $p2 --duration-s 6 $common $($cases[$name]) --log $out/$name.log"
        $null = $p.Handle
        $p.WaitForExit()
        $neg[$name] = $p.ExitCode
    }
    $h2.WaitForExit()
    $run = [ordered]@{
        transport  = $Transport
        clock      = 'real-time'
        duration_s = $DurationS
        keys       = 'generated under %TEMP% for this run and deleted afterwards (never in the repository)'
        exit_codes = [ordered]@{ host = $h.ExitCode; client1 = $c1.ExitCode; client2 = $c2.ExitCode; host2 = $h2.ExitCode }
        negative_exit_codes = $neg
    }
    [IO.File]::WriteAllText((Join-Path (Resolve-Path $out) 'run.json'), ($run | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
    python -I tools/e2e/check_secure.py $out
    if ($LASTEXITCODE -ne 0) { Write-Error "Secure E2E failed its gates (see $out/check.json)." }
} finally {
    if (Test-Path $keys) { Remove-Item -Recurse -Force $keys }  # the test keys generated above, nothing else
    Pop-Location
}
