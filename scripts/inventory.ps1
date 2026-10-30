<#
.SYNOPSIS
  Read-only game discovery + PE inventory, then evidence validation and index regeneration.
  Never writes to the game directory; outputs go to evidence/ with sanitized paths.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    python -I tools/environment/discover_game.py --repo .
    if ($LASTEXITCODE -ne 0) { throw "Game discovery failed ($LASTEXITCODE); see message above for the blocker." }
    # D-008 / FB-30-04: the GOG target build must be exactly the pinned one before any game-side work.
    python -I tools/environment/check_gog_build.py --repo .
    if ($LASTEXITCODE -eq 3) { throw 'The GOG build is not the one pinned by D-008: game-side work is blocked until it is re-checked and the owner decides.' }
    if ($LASTEXITCODE -eq 5) { throw 'The GOG build could not be fully verified (incomplete or unreadable metadata, FB-32-02): game-side work is blocked until it is re-checked.' }
    if ($LASTEXITCODE -eq 4) { Write-Warning 'The GOG release is not installed here: game-side work is blocked on this machine.' }
    elseif ($LASTEXITCODE -ne 0) { throw "GOG build check failed ($LASTEXITCODE)." }
    python -I tools/evidence/evidence.py index
    if ($LASTEXITCODE -ne 0) { throw 'Evidence validation reported errors.' }
} finally {
    Pop-Location
}
