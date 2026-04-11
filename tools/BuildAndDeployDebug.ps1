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

    Copy-Item "build\debug-msvc\ModernPhysiqueSystem.dll" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.dll") -Force
    Copy-Item "build\debug-msvc\ModernPhysiqueSystem.pdb" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.pdb") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\index.html" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\index.html") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\script.js" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\script.js") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\style.css" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\style.css") -Force
    Copy-Item "Scripts\RleyTextureBridge.pex" (Join-Path $modRoot "Scripts\RleyTextureBridge.pex") -Force
    Copy-Item "Source\Scripts\RleyTextureBridge.psc" (Join-Path $modRoot "Source\Scripts\RleyTextureBridge.psc") -Force
    Copy-Item "Source\Scripts\NiOverride.psc" (Join-Path $modRoot "Source\Scripts\NiOverride.psc") -Force
    Copy-Item "Source\Scripts\ArmorAddon.psc" (Join-Path $modRoot "Source\Scripts\ArmorAddon.psc") -Force

    if ($CompileRuntimeMorphController) {
        Copy-Item "Scripts\RleyRuntimeMorphController.pex" (Join-Path $modRoot "Scripts\RleyRuntimeMorphController.pex") -Force
        Copy-Item "Source\Scripts\RleyRuntimeMorphController.psc" (Join-Path $modRoot "Source\Scripts\RleyRuntimeMorphController.psc") -Force
    }

    Write-Host "Build + deploy completed."
}
finally {
    Pop-Location
}
