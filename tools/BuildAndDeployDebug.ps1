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
$muModRoot = "E:\Wabbajack\InstallPacks\mods\Mu Dynamic NormalMap"
$mo2OverwriteRoot = "E:\Wabbajack\InstallPacks\overwrite"
$muRuntimeDllRelativePath = "SKSE\Plugins\MuDynamicNormalMap.dll"
$muRuntimeIniRelativePath = "SKSE\Plugins\MuDynamicNormalMap.ini"
$muUpdateShaderRelativePath = "SKSE\Plugins\MuDynamicNormalMap\Shader\UpdateNormalMap.hlsl"
$muCompiledShaderRelativePath = "SKSE\Plugins\MuDynamicNormalMap\Shader\Compiled\CS_UpdateNormalMap.cso"
$expectedMuVersion = "2.1.12"
$expectedMuDllSha256 = "554BF6FF1DE74529BDBCBE97116BD715413BE045DA3161B6807B78B279D1EA31"
$expectedMuUpstreamShaderSha256 = "369AB28A5010E1990BE39EFAA8EA67290807D4FE489EF8AA1DFC0CE8CB4FD38D"
$knownMuOverlayShaderSha256 = "9632EB933D106EEB49C185628654C55AA6EFFFF0A9E7AA66A3F718F12F68F5C5"
$expectedMuRuntimeIniSha256 = "680275EE33D4BC7F59C27A520F098B241B13FA142D0AA92663ECE331B4573CD5"
$blockingProcessNames = @("SkyrimSE", "skse64_loader")

function Assert-DeploymentProcessesStopped {
    $blockingProcesses = @(Get-Process -Name $blockingProcessNames -ErrorAction SilentlyContinue)
    if ($blockingProcesses.Count -gt 0) {
        $blockingSummary = ($blockingProcesses | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ", "
        throw "Close Skyrim before deployment. Running: $blockingSummary"
    }
}

function Assert-FileSha256Match {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$DestinationPath
    )

    $sourceHash = (Get-FileHash -LiteralPath $SourcePath -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $DestinationPath -Algorithm SHA256).Hash
    if (-not [string]::Equals($sourceHash, $destinationHash, [StringComparison]::OrdinalIgnoreCase)) {
        throw "SHA-256 verification failed: source=$SourcePath destination=$DestinationPath"
    }
}

function Get-FileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-FileSha256Value {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedHash,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $actualHash = Get-FileSha256 $Path
    if (-not [string]::Equals($actualHash, $ExpectedHash, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description has an unsupported SHA-256: path=$Path expected=$ExpectedHash actual=$actualHash"
    }
}

function Assert-PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $resolvedRoot = [IO.Path]::GetFullPath($Root) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the intended mod root: path=$resolvedPath root=$resolvedRoot"
    }
}

Assert-DeploymentProcessesStopped

Push-Location $repoRoot
try {
    # Runtime deployment deliberately uses Release. RaceMenu's face API passes
    # an STL-owning SKEEFixedString across the DLL boundary, so a Debug CRT/STL
    # build is ABI-incompatible even though it links and loads successfully.
    cmd /c .\build_release.bat
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed."
    }

    # Compile the bridge before the test suite: static safety validates the
    # deployed PEX ABI against the native callbacks added by this source tree.
    & $papyrusCompiler `
        "Source\Scripts\RleyTextureBridge.psc" `
        "-import=$papyrusImports" `
        "-output=$(Join-Path $repoRoot 'Scripts')" `
        "-flags=$papyrusFlags"
    if ($LASTEXITCODE -ne 0) {
        throw "Papyrus compile failed for RleyTextureBridge."
    }

    $testDirectory = Join-Path $repoRoot "build\release-msvc"
    $requiredTests = @(
        "mps_tests",
        "mps_face_route_static_safety",
        "mps_normal_map_bridge_static_safety",
        "mps_racemenu_weight_sync_static_safety",
        "mps_deploy_static_safety",
        "mps_shared_face_morph_static_safety",
        "mps_face_slider_asset_safety"
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
        "build\release-msvc\ModernPhysiqueSystem.dll",
        "build\release-msvc\ModernPhysiqueSystem.pdb",
        "SKSE\Plugins\ModernPhysiqueSystem.ini",
        "SKSE\Plugins\MuDynamicNormalMap.ini",
        "PrismaUI\views\RleyBodyMorphs\index.html",
        "PrismaUI\views\RleyBodyMorphs\script.js",
        "PrismaUI\views\RleyBodyMorphs\style.css",
        "Source\Scripts\RleyTextureBridge.psc",
        "Source\Scripts\MuDynamicNormalMap.psc",
        "Source\Scripts\NiOverride.psc",
        "Source\Scripts\ArmorAddon.psc"
    )
    $muRestoreAssetRelativePaths = @(
        "Meshes\!COR\Hands\femalehands_1.nif",
        "Meshes\!COR\Feet\femalefeet_1.nif"
    )
    $mpsMuAssetBackupRoot = Join-Path $repoRoot "backups\deploy_original_mps_mu_assets"
    foreach ($assetRelativePath in $muRestoreAssetRelativePaths) {
        $requiredDeploySources += Join-Path $mpsMuAssetBackupRoot $assetRelativePath
    }
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

    $muDllPath = Join-Path $muModRoot $muRuntimeDllRelativePath
    $muIniDestinationPath = Join-Path $muModRoot $muRuntimeIniRelativePath
    $muShaderDestinationPath = Join-Path $muModRoot $muUpdateShaderRelativePath
    $muCompiledShaderPath = Join-Path $muModRoot $muCompiledShaderRelativePath
    $muOverwriteCompiledShaderPath = Join-Path $mo2OverwriteRoot $muCompiledShaderRelativePath
    foreach ($requiredMuRuntimePath in @($muDllPath, $muIniDestinationPath, $muShaderDestinationPath)) {
        if (-not (Test-Path -LiteralPath $requiredMuRuntimePath -PathType Leaf)) {
            throw "Deploy aborted before copying; Mu 2.1.12 runtime input is missing: $requiredMuRuntimePath"
        }
    }

    Assert-FileSha256Value $muDllPath $expectedMuDllSha256 "Mu Dynamic NormalMap DLL"
    $actualMuVersion = (Get-Item -LiteralPath $muDllPath).VersionInfo.FileVersion
    if (-not [string]::Equals($actualMuVersion, $expectedMuVersion, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsupported Mu Dynamic NormalMap version: expected=$expectedMuVersion actual=$actualMuVersion"
    }

    $repoMuIniPath = Join-Path $repoRoot $muRuntimeIniRelativePath
    Assert-FileSha256Value $repoMuIniPath $expectedMuRuntimeIniSha256 "MPS Mu runtime profile"
    $muRuntimeBackupRoot = Join-Path $repoRoot "backups\deploy_original_mu_2_1_12"
    $muShaderBackupPath = Join-Path $muRuntimeBackupRoot $muUpdateShaderRelativePath
    $muIniBackupPath = Join-Path $muRuntimeBackupRoot $muRuntimeIniRelativePath
    Assert-FileSha256Value $muShaderBackupPath $expectedMuUpstreamShaderSha256 "Backed-up upstream Mu shader"
    $currentMuShaderHash = Get-FileSha256 $muShaderDestinationPath
    if (-not [string]::Equals($currentMuShaderHash, $expectedMuUpstreamShaderSha256, [StringComparison]::OrdinalIgnoreCase) -and
        -not [string]::Equals($currentMuShaderHash, $knownMuOverlayShaderSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsupported Mu shader; refusing to overwrite an unknown version: path=$muShaderDestinationPath hash=$currentMuShaderHash"
    }

    Assert-PathWithinRoot $muCompiledShaderPath $muModRoot
    Assert-PathWithinRoot $muOverwriteCompiledShaderPath $mo2OverwriteRoot

    # The build and Papyrus compile can take long enough for either process to
    # be opened after the initial guard. Close that window before any write.
    Assert-DeploymentProcessesStopped

    New-Item -ItemType Directory -Path (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "SKSE\Plugins") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "SKSE\Plugins\MuDynamicNormalMap") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "Scripts") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "interface") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "Meshes\!COR\Hands") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $modRoot "Meshes\!COR\Feet") -Force | Out-Null

    # The new PEX keeps the legacy entrypoint, so copying it before the DLL
    # avoids a transient new-DLL/old-PEX ABI mismatch during deployment.
    Copy-Item "Scripts\RleyTextureBridge.pex" (Join-Path $modRoot "Scripts\RleyTextureBridge.pex") -Force
    Copy-Item "interface\racesex_menu.swf" (Join-Path $modRoot "interface\racesex_menu.swf") -Force
    Copy-Item "build\release-msvc\ModernPhysiqueSystem.dll" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.dll") -Force
    Copy-Item "build\release-msvc\ModernPhysiqueSystem.pdb" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.pdb") -Force
    Copy-Item "SKSE\Plugins\ModernPhysiqueSystem.ini" (Join-Path $modRoot "SKSE\Plugins\ModernPhysiqueSystem.ini") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\index.html" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\index.html") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\script.js" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\script.js") -Force
    Copy-Item "PrismaUI\views\RleyBodyMorphs\style.css" (Join-Path $modRoot "PrismaUI\views\RleyBodyMorphs\style.css") -Force

    foreach ($assetRelativePath in $muRestoreAssetRelativePaths) {
        $sourcePath = Join-Path $mpsMuAssetBackupRoot $assetRelativePath
        $destinationPath = Join-Path $modRoot $assetRelativePath
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        Assert-FileSha256Match $sourcePath $destinationPath
    }

    if (-not (Test-Path -LiteralPath $muShaderBackupPath -PathType Leaf)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $muShaderBackupPath) -Force | Out-Null
        Copy-Item -LiteralPath $muShaderDestinationPath -Destination $muShaderBackupPath
        Assert-FileSha256Value $muShaderBackupPath $expectedMuUpstreamShaderSha256 "Backed-up upstream Mu shader"
    }
    if (-not (Test-Path -LiteralPath $muIniBackupPath -PathType Leaf)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $muIniBackupPath) -Force | Out-Null
        Copy-Item -LiteralPath $muIniDestinationPath -Destination $muIniBackupPath
        Assert-FileSha256Match $muIniDestinationPath $muIniBackupPath
    }

    Copy-Item -LiteralPath $muShaderBackupPath -Destination $muShaderDestinationPath -Force
    Assert-FileSha256Match $muShaderBackupPath $muShaderDestinationPath
    Copy-Item -LiteralPath $repoMuIniPath -Destination $muIniDestinationPath -Force
    Assert-FileSha256Match $repoMuIniPath $muIniDestinationPath
    if (Test-Path -LiteralPath $muCompiledShaderPath -PathType Leaf) {
        Remove-Item -LiteralPath $muCompiledShaderPath -Force
    }
    if (Test-Path -LiteralPath $muOverwriteCompiledShaderPath -PathType Leaf) {
        Remove-Item -LiteralPath $muOverwriteCompiledShaderPath -Force
    }

    foreach ($conditionName in @("ModernPhysiqueDetailsMale.ini", "ModernPhysiqueDetailsFemale.ini")) {
        $conditionPath = Join-Path $modRoot "SKSE\Plugins\MuDynamicNormalMap\$conditionName"
        Assert-PathWithinRoot $conditionPath $modRoot
        Remove-Item -LiteralPath $conditionPath -Force -ErrorAction SilentlyContinue
    }
    $staleMppMuIniPath = Join-Path $modRoot $muRuntimeIniRelativePath
    Assert-PathWithinRoot $staleMppMuIniPath $modRoot
    Remove-Item -LiteralPath $staleMppMuIniPath -Force -ErrorAction SilentlyContinue
    foreach ($overlayAssetRelativePath in @(
        "Textures\RleyBodyMorphs\Details",
        "Textures\RleyBodyMorphs\MuMask"
    )) {
        $overlayAssetPath = Join-Path $modRoot $overlayAssetRelativePath
        Assert-PathWithinRoot $overlayAssetPath $modRoot
        Remove-Item -LiteralPath $overlayAssetPath -Recurse -Force -ErrorAction SilentlyContinue
    }
    # Earlier prototypes mirrored a single detail texture into the automatic
    # Mu proxy paths. Those mirrors bypass per-morph selection and can affect
    # every body update, so remove them when upgrading an existing profile.
    Remove-Item -LiteralPath (Join-Path $modRoot "Textures\actors\character\male\malebody_1_n.dds") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $modRoot "Textures\!COR\Body\FemaleBody_1_n.dds") -Force -ErrorAction SilentlyContinue
    Copy-Item "Source\Scripts\RleyTextureBridge.psc" (Join-Path $modRoot "Source\Scripts\RleyTextureBridge.psc") -Force
    Copy-Item "Source\Scripts\MuDynamicNormalMap.psc" (Join-Path $modRoot "Source\Scripts\MuDynamicNormalMap.psc") -Force
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
