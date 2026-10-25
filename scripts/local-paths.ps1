# Evidence never holds absolute local paths (project rule). Hide-LocalPaths replaces the repository root, %TEMP% and
# the user profile by placeholders, then ANY remaining absolute path (a drive path X:\... or a UNC path \\host\...)
# by <abs-path> (Codex review of PR #20: a build directory outside those three prefixes leaked).
# Dot-source it:  . (Join-Path $PSScriptRoot 'local-paths.ps1')
function Hide-LocalPaths([string]$Text, [string]$Root) {
    if (-not $Text) { return $Text }
    foreach ($p in @(@($Root, '.'), @($env:TEMP, '%TEMP%'), @($env:USERPROFILE, '%USERPROFILE%'))) {
        if ($p[0]) { $Text = $Text -replace [regex]::Escape($p[0]), $p[1] }
    }
    # A drive path not preceded by a letter (so "https://" and "%TEMP%" are untouched), or a UNC path.
    return $Text -replace '(?<![A-Za-z0-9])[A-Za-z]:[\\/][^\s''"<>|]*', '<abs-path>' -replace '\\\\[^\s''"<>|\\]+\\[^\s''"<>|]*', '<abs-path>'
}
