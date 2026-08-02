[CmdletBinding()]
param(
    [switch]$CompileRuntimeMorphController
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$papyrusCompiler = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Papyrus Compiler\PapyrusCompiler.exe"
$papyrusFlags = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data\Scripts\Source\TESV_Papyrus_Flags.flg"
$papyrusImports = @(
    (Join-Path $repoRoot "Source\Scripts"),
    "E:\Wabbajack\InstallPacks\overwrite\source\scripts",
    "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data\Scripts\Source"
) -join ";"

$modRoot = "E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker"

Push-Location $repoRoot
try {
    cmd /c build_temp.bat
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed."
    }

    & $papyrusCompiler `
        "Source\Scripts\RleyTextureBridge.psc" `
        "-import=$papyrusImports" `
        "-output=$(Join-Path $repoRoot 'Scripts')" `
        "-flags=$papyrusFlags"
    if ($LASTEXITCODE -ne 0) {
        throw "Papyrus compile failed for RleyTextureBridge."
    }

    if ($CompileRuntimeMorphController) {
        & $papyrusCompiler `
            "Source\Scripts\RleyRuntimeMorphController.psc" `
            "-import=$papyrusImports" `
            "-output=$(Join-Path $repoRoot 'Scripts')" `
            "-flags=$papyrusFlags"
        if ($LASTEXITCODE -ne 0) {
            throw "Papyrus compile failed for RleyRuntimeMorphController."
        }
    }

    New-Item -ItemType Directory -Path (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "SKSE\Plugins") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "Scripts") -Force | Out-Null

    # The new PEX keeps the legacy entrypoint, so copying it before the DLL
    # avoids a transient new-DLL/old-PEX ABI mismatch during deployment.
    Copy-Item "Scripts\RleyTextureBridge.pex" (Join-Path $modRoot "Scripts\RleyTextureBridge.pex") -Force
    Copy-Item "build\debug-msvc\ModernPhysiqueSystem.dll" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.dll") -Force
    Copy-Item "build\debug-msvc\ModernPhysiqueSystem.pdb" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.pdb") -Force
    Copy-Item "SKSE\Plugins\ModernPhysiqueSystem.ini" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.ini") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\index.html" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\index.html") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\script.js" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\script.js") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\style.css" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\style.css") -Force
    Copy-Item "Source\Scripts\RleyTextureBridge.psc" (Join-Path $modRoot "Source\Scripts\RleyTextureBridge.psc") -Force
    Copy-Item "Source\Scripts\NiOverride.psc" (Join-Path $modRoot "Source\Scripts\NiOverride.psc") -Force
    Copy-Item "Source\Scripts\ArmorAddon.psc" (Join-Path $modRoot "Source\Scripts\ArmorAddon.psc") -Force

    if ($CompileRuntimeMorphController) {
        Copy-Item "Scripts\RleyRuntimeMorphController.pex" (Join-Path $modRoot "Scripts\RleyRuntimeMorphController.pex") -Force
        Copy-Item "Source\Scripts\RleyRuntimeMorphController.psc" (Join-Path $modRoot "Source\Scripts\RleyRuntimeMorphController.psc") -Force
    }

    # Deploy mppatcher-cli.exe + MPPatcher.exe
    $bsModDir = Join-Path $modRoot "CalienteTools\BodySlide"
    New-Item -ItemType Directory -Force $bsModDir | Out-Null

    $mppatcherCliSrc = "E:\SkyrimDev\BodySlide-and-Outfit-Studio\mppatcher\build\Release\mppatcher-cli.exe"
    if (Test-Path $mppatcherCliSrc) {
        Copy-Item $mppatcherCliSrc "$bsModDir\mppatcher-cli.exe" -Force
        Write-Host "Deployed mppatcher-cli.exe"
    }

    $mppatcherExeSrc = "E:\SkyrimDev\MPPatcher\dist\MPPatcher.exe"
    if (Test-Path $mppatcherExeSrc) {
        Copy-Item $mppatcherExeSrc "$bsModDir\MPPatcher.exe" -Force
        Write-Host "Deployed MPPatcher.exe"
    }

    Write-Host "Build + deploy completed."
}
finally {
    Pop-Location
}
