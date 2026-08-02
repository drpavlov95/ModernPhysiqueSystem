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
$blockingProcessNames = @("ModOrganizer", "SkyrimSE", "skse64_loader")

function Assert-DeploymentProcessesStopped {
    $blockingProcesses = @(Get-Process -Name $blockingProcessNames -ErrorAction SilentlyContinue)
    if ($blockingProcesses.Count -gt 0) {
        $blockingSummary = ($blockingProcesses | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ", "
        throw "Close Mod Organizer and Skyrim before deployment. Running: $blockingSummary"
    }
}

Assert-DeploymentProcessesStopped

Push-Location $repoRoot
try {
    cmd /c build_temp.bat
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed."
    }

    $testDirectory = Join-Path $repoRoot "build\debug-msvc"
    $requiredTests = @(
        "mps_tests",
        "mps_face_route_static_safety",
        "mps_normal_map_bridge_static_safety",
        "mps_racemenu_weight_sync_static_safety",
        "mps_deploy_static_safety"
    )
    $testManifestJson = & ctest --test-dir $testDirectory --show-only=json-v1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect the pre-deploy test manifest."
    }
    try {
        $testManifest = ($testManifestJson -join [Environment]::NewLine) | ConvertFrom-Json
    }
    catch {
        throw "Unable to parse the pre-deploy test manifest: $($_.Exception.Message)"
    }
    $availableTests = @($testManifest.tests | ForEach-Object { $_.name })
    $missingRequiredTests = @($requiredTests | Where-Object { $_ -notin $availableTests })
    if ($missingRequiredTests.Count -gt 0) {
        throw "Pre-deploy test manifest is incomplete: $($missingRequiredTests -join ', ')"
    }

    & ctest --test-dir $testDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Pre-deploy test suite failed."
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

    $requiredDeploySources = @(
        "Scripts\RleyTextureBridge.pex",
        "interface\racesex_menu.swf",
        "build\debug-msvc\ModernPhysiqueSystem.dll",
        "build\debug-msvc\ModernPhysiqueSystem.pdb",
        "SKSE\Plugins\ModernPhysiqueSystem.ini",
        "PrismaUI\views\RleyBodyMorphs\index.html",
        "PrismaUI\views\RleyBodyMorphs\script.js",
        "PrismaUI\views\RleyBodyMorphs\style.css",
        "Source\Scripts\RleyTextureBridge.psc",
        "Source\Scripts\NiOverride.psc",
        "Source\Scripts\ArmorAddon.psc"
    )
    if ($CompileRuntimeMorphController) {
        $requiredDeploySources += @(
            "Scripts\RleyRuntimeMorphController.pex",
            "Source\Scripts\RleyRuntimeMorphController.psc"
        )
    }
    $missingDeploySources = @(
        $requiredDeploySources | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }
    )
    if ($missingDeploySources.Count -gt 0) {
        throw "Deploy aborted before copying; missing artifacts: $($missingDeploySources -join ', ')"
    }

    # The build and Papyrus compile can take long enough for either process to
    # be opened after the initial guard. Close that window before any write.
    Assert-DeploymentProcessesStopped

    New-Item -ItemType Directory -Path (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "SKSE\Plugins") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "Scripts") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "interface") -Force | Out-Null

    # The new PEX keeps the legacy entrypoint, so copying it before the DLL
    # avoids a transient new-DLL/old-PEX ABI mismatch during deployment.
    Copy-Item "Scripts\RleyTextureBridge.pex" (Join-Path $modRoot "Scripts\RleyTextureBridge.pex") -Force
    Copy-Item "interface\racesex_menu.swf" (Join-Path $modRoot "interface\racesex_menu.swf") -Force
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
