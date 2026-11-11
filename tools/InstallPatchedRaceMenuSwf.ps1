[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ModRoot,
    [string]$FfdecCli,
    [string]$PythonExe = "python"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($FfdecCli)) {
    $FfdecCli = Join-Path $repoRoot "_tmp_tools\ffdec_26.0.0\ffdec-cli.exe"
}

$stockSwfPath = Join-Path $ModRoot "interface\racesex_menu.swf"
$markerPath = "$stockSwfPath.mps-patched"

# Second-run safety (Review Focus / C2 fix): a marker's mere existence does
# not prove the file it names is still what we produced -- RaceMenu could
# have been updated/reinstalled since, replacing it with a fresh stock SWF.
# Compare the marker's recorded hash against the file's current hash before
# trusting it; only an exact match means "nothing to do".
if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
    $markerLines = Get-Content -LiteralPath $markerPath
    $markerHash = $markerLines[0]
    if ((Test-Path -LiteralPath $stockSwfPath -PathType Leaf) -and
        (Get-FileHash -LiteralPath $stockSwfPath -Algorithm SHA256).Hash -eq $markerHash) {
        Write-Host "Already patched (hash matches marker at $markerPath); nothing to do."
        exit 0
    }
    Write-Host "Marker at $markerPath is stale ($stockSwfPath changed since); removing it and treating the current file as fresh stock."
    Remove-Item -LiteralPath $markerPath -Force
}

if (-not (Test-Path -LiteralPath $stockSwfPath -PathType Leaf)) {
    throw "Stock RaceMenu SWF not found: $stockSwfPath"
}
if (-not (Test-Path -LiteralPath $FfdecCli -PathType Leaf)) {
    throw "FFDec CLI not found: $FfdecCli"
}
# I6: fail before touching anything if the other external tool this depends
# on (beyond FFDec, already checked above) is missing. GNU patch is not
# checked here -- a plain PowerShell PATH commonly lacks Git's usr\bin even
# when git itself resolves, so tools/racemenu_swf_patch/diff_apply.py's
# find_patch_executable() does its own Git-relative fallback instead of a
# bare PATH check.
if (-not (Get-Command $PythonExe -ErrorAction SilentlyContinue)) {
    throw "Required command not found on PATH: $PythonExe"
}

$tempOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("racesex_menu_patched_{0}.swf" -f ([guid]::NewGuid()))

Push-Location $repoRoot
try {
    $pythonOutput = & $PythonExe -m tools.racemenu_swf_patch.build_patched_swf $FfdecCli $stockSwfPath $tempOutput 2>&1
    $pythonExitCode = $LASTEXITCODE
    $pythonOutput | ForEach-Object { Write-Host $_ }
    if ($pythonExitCode -ne 0) {
        throw "RaceMenu SWF patch build failed (exit code $pythonExitCode). $stockSwfPath was not modified."
    }
}
finally {
    Pop-Location
}

# I6: take the backup only once the build has actually succeeded, so a
# failed attempt (every attempt, until real diffs/hashes exist) does not
# leave a redundant backup copy in the live mod's asset folder.
# Collision-safe naming (Review Focus): fall back to a counter suffix if two
# installs land in the same second.
$backupPath = Join-Path $ModRoot ("interface\racesex_menu.swf.backup-{0}" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
$backupSuffix = 1
while (Test-Path -LiteralPath $backupPath -PathType Leaf) {
    $backupPath = Join-Path $ModRoot ("interface\racesex_menu.swf.backup-{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $backupSuffix)
    $backupSuffix += 1
}
Copy-Item -LiteralPath $stockSwfPath -Destination $backupPath

# I3: record which RaceMenu version was matched (printed by
# build_patched_swf.main as "VERSION:<label>") alongside the output hash, so
# a future bug report can say what was patched instead of just that
# something was.
$versionLine = $pythonOutput | Where-Object { $_ -match "^VERSION:(.+)$" } | Select-Object -Last 1
$matchedVersion = if ($versionLine) { $versionLine -replace "^VERSION:", "" } else { "unknown" }

$hash = (Get-FileHash -LiteralPath $tempOutput -Algorithm SHA256).Hash
Move-Item -LiteralPath $tempOutput -Destination $stockSwfPath -Force
Set-Content -LiteralPath $markerPath -Value @($hash, $matchedVersion)

Write-Host "Installed patched RaceMenu SWF: $stockSwfPath"
Write-Host "Matched RaceMenu version: $matchedVersion"
Write-Host "Backup: $backupPath"
Write-Host "SHA256: $hash"
Write-Host "To restore the original stock SWF: copy '$backupPath' back over '$stockSwfPath' and delete '$markerPath'."
