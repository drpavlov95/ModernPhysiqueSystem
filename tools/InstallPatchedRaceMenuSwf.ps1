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

# Second-run safety (Review Focus): a marker left by a prior successful
# install means this file is already the patched output, not a fresh stock
# SWF — re-diffing it would fail closed anyway (Task 4's --fuzz=0), but
# this gives a clearer message before even trying.
if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
    Write-Host "Already patched (marker found at $markerPath); nothing to do."
    exit 0
}

if (-not (Test-Path -LiteralPath $stockSwfPath -PathType Leaf)) {
    throw "Stock RaceMenu SWF not found: $stockSwfPath"
}
if (-not (Test-Path -LiteralPath $FfdecCli -PathType Leaf)) {
    throw "FFDec CLI not found: $FfdecCli"
}

# Collision-safe backup naming (Review Focus): fall back to a counter suffix
# if two installs land in the same second.
$backupPath = Join-Path $ModRoot ("interface\racesex_menu.swf.backup-{0}" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
$backupSuffix = 1
while (Test-Path -LiteralPath $backupPath -PathType Leaf) {
    $backupPath = Join-Path $ModRoot ("interface\racesex_menu.swf.backup-{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $backupSuffix)
    $backupSuffix += 1
}
Copy-Item -LiteralPath $stockSwfPath -Destination $backupPath

$tempOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("racesex_menu_patched_{0}.swf" -f ([guid]::NewGuid()))

Push-Location $repoRoot
try {
    & $PythonExe -m tools.racemenu_swf_patch.build_patched_swf $FfdecCli $stockSwfPath $tempOutput
    if ($LASTEXITCODE -ne 0) {
        throw "RaceMenu SWF patch build failed (exit code $LASTEXITCODE). $stockSwfPath was not modified; backup at $backupPath."
    }
}
finally {
    Pop-Location
}

$hash = (Get-FileHash -LiteralPath $tempOutput -Algorithm SHA256).Hash
Move-Item -LiteralPath $tempOutput -Destination $stockSwfPath -Force
Set-Content -LiteralPath $markerPath -Value $hash -NoNewline

Write-Host "Installed patched RaceMenu SWF: $stockSwfPath"
Write-Host "Backup: $backupPath"
Write-Host "SHA256: $hash"
