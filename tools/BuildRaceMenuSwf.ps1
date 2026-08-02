[CmdletBinding()]
param(
    [string]$FfdecCli,
    [switch]$UpdateRepositoryArtifact
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($FfdecCli)) {
    $FfdecCli = Join-Path $repoRoot "_tmp_tools\ffdec_26.0.0\ffdec-cli.exe"
}

$inputSwf = Join-Path $repoRoot "interface\racesex_menu.swf"
$sourceAs = Join-Path $repoRoot "reference\RaceMenu\patched\RaceMenu.as"
$outputDirectory = Join-Path $repoRoot "build\racemenu-swf"
$outputSwf = Join-Path $outputDirectory "racesex_menu.swf"

foreach ($requiredPath in @($FfdecCli, $inputSwf, $sourceAs)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required RaceMenu build input is missing: $requiredPath"
    }
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

& $FfdecCli `
    -onerror abort `
    -replace `
    $inputSwf `
    $outputSwf `
    '\__Packages\RaceMenu' `
    $sourceAs
if ($LASTEXITCODE -ne 0) {
    throw "FFDec failed to compile RaceMenu.as (exit code $LASTEXITCODE)."
}

& $FfdecCli -dumpSWF $outputSwf | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "FFDec could not parse the compiled RaceMenu SWF (exit code $LASTEXITCODE)."
}

if ($UpdateRepositoryArtifact) {
    Copy-Item -LiteralPath $outputSwf -Destination $inputSwf -Force
}

$artifactPath = if ($UpdateRepositoryArtifact) { $inputSwf } else { $outputSwf }
$artifact = Get-Item -LiteralPath $artifactPath
$hash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
Write-Host "RaceMenu SWF ready: $($artifact.FullName)"
Write-Host "Size: $($artifact.Length) bytes"
Write-Host "SHA256: $hash"
