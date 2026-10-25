# Evidence never holds absolute local paths (project rule). Hide-LocalPaths replaces the repository root, %TEMP% and
# the user profile by placeholders, then ANY remaining absolute path (a drive path X:\... or a UNC path \\host\...)
# by <abs-path> (Codex review of PR #20: a build directory outside those three prefixes leaked).
# FB-21-03: paths may contain spaces and non-ASCII letters. A quoted path is hidden up to its closing quote. An
# unquoted one is hidden up to its LAST separator plus the file name after it, so "D:\Outra Pasta\sub dir\unit.json now"
# becomes "<abs-path> now". shortcut: an unquoted path also swallows plain words up to a later "\" or "/" on the same
# line (over-hiding, never leaking); scripts quote the paths they put in messages.
# Dot-source it:  . (Join-Path $PSScriptRoot 'local-paths.ps1')
function Hide-LocalPaths([string]$Text, [string]$Root) {
    if (-not $Text) { return $Text }
    foreach ($p in @(@($Root, '.'), @($env:TEMP, '%TEMP%'), @($env:USERPROFILE, '%USERPROFILE%'))) {
        if ($p[0]) { $Text = $Text -replace [regex]::Escape($p[0]), $p[1] }
    }
    $start = '(?:(?<![A-Za-z0-9])[A-Za-z]:[\\/]|\\\\[^\\/\s''"<>|]+[\\/])'  # a drive (not inside a word or URL) or \\host\
    $Text = $Text -replace "'$start[^'\r\n]*'", "'<abs-path>'" -replace """$start[^""\r\n]*""", '"<abs-path>"'
    return $Text -replace "$start(?:[^\\/\r\n'""<>|]*[\\/])*[^\s\\/'""<>|]*", '<abs-path>'
}
