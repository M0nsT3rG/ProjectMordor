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
    python -I tools/evidence/evidence.py index
    if ($LASTEXITCODE -ne 0) { throw 'Evidence validation reported errors.' }
} finally {
    Pop-Location
}
