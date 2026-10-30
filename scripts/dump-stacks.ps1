# KI-40 diagnostics: write a minidump of one of OUR stuck processes and the symbolized stacks of all its threads.
# Local only (rule 7): the dump and the stack text stay in -OutDir (default %TEMP%\mordor-dumps), never in the
# repository and never uploaded. Symbols: our build directory, plus Microsoft's public symbol server for Windows DLLs
# (it receives only the identifiers of Microsoft modules). Needs WinDbg (winget Microsoft.WinDbg; owner-approved 2026-10-24).
# A process stuck inside ExitProcess may refuse a normal handle (seen 2026-10-25): cdb's non-invasive attach (-pv)
# still reads it, so it is used first; MiniDumpWriteDump is only the fallback without WinDbg.
#   ./scripts/dump-stacks.ps1 -ProcessId 1234 [-OutDir DIR] [-BuildDir build-gns]
param(
    [Parameter(Mandatory)][int]$ProcessId,
    [string]$OutDir = (Join-Path $env:TEMP 'mordor-dumps'),
    [string]$BuildDir = 'build-gns',
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$p = Get-Process -Id $ProcessId
# ki35*: the KI-35 measurement probes (tools/ki35/bounded.py)
if ($p.ProcessName -notlike 'mordor*' -and $p.ProcessName -notlike 'ki35*') { throw "refusing to dump '$($p.ProcessName)': only this project's processes" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$name = "{0}-{1}-{2}" -f $p.ProcessName, $ProcessId, (Get-Date -Format 'yyyyMMdd-HHmmss')
$dmp = Join-Path $OutDir "$name.dmp"
$stacks = Join-Path $OutDir "$name.stacks.txt"
$pkg = Get-AppxPackage -Name 'Microsoft.WinDbg*' | Select-Object -First 1
$cdb = if ($pkg) { Join-Path $pkg.InstallLocation 'amd64\cdb.exe' } else { $null }
if ($cdb -and (Test-Path $cdb)) {
    $sym = "srv*$(Join-Path $env:TEMP 'mordor-symcache')*https://msdl.microsoft.com/download/symbols;$(Join-Path $root "$BuildDir\$Config")"
    $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'  # cdb writes warnings to stderr
    & $cdb -pv -p $ProcessId -y $sym -c ".reload /f; ~*kc 30; .dump /ma `"$dmp`"; q" *> $stacks
    $ErrorActionPreference = $eap
} else {
    if (-not ('K40.D' -as [type])) {
        Add-Type -Namespace K40 -Name D -MemberDefinition '[DllImport("dbghelp.dll", SetLastError=true)] public static extern bool MiniDumpWriteDump(System.IntPtr hProcess, uint pid, Microsoft.Win32.SafeHandles.SafeFileHandle hFile, uint type, System.IntPtr e, System.IntPtr u, System.IntPtr c);'
    }
    if (-not $p.Handle) { throw "no handle to pid $ProcessId and no WinDbg: cannot dump" }
    $f = [IO.File]::Create($dmp)
    try { $ok = [K40.D]::MiniDumpWriteDump($p.Handle, [uint32]$ProcessId, $f.SafeFileHandle, 0x2 -bor 0x4, [IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero) }  # full memory + handles
    finally { $f.Close() }
    if (-not $ok) { throw "MiniDumpWriteDump failed for pid $ProcessId" }
    Write-Warning "WinDbg not found: dump written without stacks"
}
Write-Host "dump: $dmp"
if (Test-Path $stacks) { Write-Host "stacks: $stacks" }
