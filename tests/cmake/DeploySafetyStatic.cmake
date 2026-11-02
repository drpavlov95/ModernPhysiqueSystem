if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(MPS_CMAKE_FILE "${MPS_SOURCE_DIR}/CMakeLists.txt")
set(MPS_PRESETS_FILE "${MPS_SOURCE_DIR}/CMakePresets.json")
set(MPS_DEPLOY_SCRIPT "${MPS_SOURCE_DIR}/tools/BuildAndDeployDebug.ps1")

foreach(required_path IN ITEMS "${MPS_CMAKE_FILE}" "${MPS_PRESETS_FILE}" "${MPS_DEPLOY_SCRIPT}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Deploy-safety input is missing: ${required_path}")
    endif()
endforeach()

file(READ "${MPS_CMAKE_FILE}" cmake_source)
file(READ "${MPS_PRESETS_FILE}" presets_source)
file(READ "${MPS_DEPLOY_SCRIPT}" deploy_source)
string(REGEX REPLACE "[ \t\r\n]+" "" compact_cmake "${cmake_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_presets "${presets_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_deploy "${deploy_source}")

string(
    FIND
    "${compact_cmake}"
    "option(MPS_ENABLE_POST_BUILD_DEPLOY\"AllowCMakepost-buildcommandstocopytheSKSEplugin\"OFF)"
    deploy_option_offset
)
if(deploy_option_offset EQUAL -1)
    message(FATAL_ERROR "CMake post-build deployment must default to OFF")
endif()
foreach(forbidden_environment IN ITEMS "ENV{SKYRIM_FOLDER}" "ENV{SKYRIM_MODS_FOLDER}")
    string(FIND "${compact_cmake}" "${forbidden_environment}" environment_offset)
    if(environment_offset GREATER -1)
        message(FATAL_ERROR "Implicit deploy environment hook is forbidden: ${forbidden_environment}")
    endif()
endforeach()

set(deploy_gate "if(MPS_ENABLE_POST_BUILD_DEPLOY)")
string(FIND "${compact_cmake}" "${deploy_gate}" first_deploy_gate_offset)
if(first_deploy_gate_offset EQUAL -1)
    message(FATAL_ERROR "CMake deploy configuration gate is missing")
endif()
string(LENGTH "${deploy_gate}" deploy_gate_length)
math(EXPR after_first_deploy_gate "${first_deploy_gate_offset} + ${deploy_gate_length}")
string(SUBSTRING "${compact_cmake}" ${after_first_deploy_gate} -1 remaining_cmake)
string(FIND "${remaining_cmake}" "${deploy_gate}" second_relative_gate_offset)
if(second_relative_gate_offset EQUAL -1)
    message(FATAL_ERROR "CMake post-build commands are not protected by the explicit deploy gate")
endif()
math(EXPR second_deploy_gate_offset "${after_first_deploy_gate} + ${second_relative_gate_offset}")
string(FIND "${compact_cmake}" "add_custom_command(" first_custom_command_offset)
if(
    first_custom_command_offset EQUAL -1
    OR second_deploy_gate_offset GREATER_EQUAL first_custom_command_offset
)
    message(FATAL_ERROR "CMake deploy gate must precede every post-build copy command")
endif()

set(
    preset_opt_out
    "\"MPS_ENABLE_POST_BUILD_DEPLOY\":{\"type\":\"BOOL\",\"value\":false}"
)
string(FIND "${compact_presets}" "${preset_opt_out}" first_preset_opt_out)
if(first_preset_opt_out EQUAL -1)
    message(FATAL_ERROR "Debug/release presets must explicitly disable deployment")
endif()
string(LENGTH "${preset_opt_out}" preset_opt_out_length)
math(EXPR after_first_preset_opt_out "${first_preset_opt_out} + ${preset_opt_out_length}")
string(SUBSTRING "${compact_presets}" ${after_first_preset_opt_out} -1 remaining_presets)
string(FIND "${remaining_presets}" "${preset_opt_out}" second_preset_opt_out)
if(second_preset_opt_out EQUAL -1)
    message(FATAL_ERROR "Both Debug and Release presets must disable deployment")
endif()

set(
    preset_tests_enabled
    "\"MPS_BUILD_TESTS\":{\"type\":\"BOOL\",\"value\":true}"
)
string(FIND "${compact_presets}" "${preset_tests_enabled}" first_preset_tests_enabled)
if(first_preset_tests_enabled EQUAL -1)
    message(FATAL_ERROR "Debug/release presets must explicitly enable tests")
endif()
string(LENGTH "${preset_tests_enabled}" preset_tests_enabled_length)
math(EXPR after_first_preset_tests_enabled "${first_preset_tests_enabled} + ${preset_tests_enabled_length}")
string(SUBSTRING "${compact_presets}" ${after_first_preset_tests_enabled} -1 remaining_test_presets)
string(FIND "${remaining_test_presets}" "${preset_tests_enabled}" second_preset_tests_enabled)
if(second_preset_tests_enabled EQUAL -1)
    message(FATAL_ERROR "Both Debug and Release presets must enable tests")
endif()

set(required_deploy_fragments
    "Get-Process-Name$blockingProcessNames-ErrorActionSilentlyContinue"
    "throw\"CloseSkyrimbeforedeployment."
    "cmd/c"
    "build\\release-msvc\\ModernPhysiqueSystem.pdb"
    "--show-only=json-v1"
    "if($missingRequiredTests.Count-gt0)"
    "if($missingDeploySources.Count-gt0)"
    "Textures\\actors\\character\\male\\malebody_1_n.dds"
    "Textures\\!COR\\Body\\FemaleBody_1_n.dds"
    "Meshes\\!COR\\Hands\\femalehands_1.nif"
    "Meshes\\!COR\\Feet\\femalefeet_1.nif"
    "$muRestoreAssetRelativePaths"
    "backups\\deploy_original_mps_mu_assets"
    "$muModRoot=\"E:\\Wabbajack\\InstallPacks\\mods\\MuDynamicNormalMap\""
    "$expectedMuVersion=\"2.1.12\""
    "$expectedMuDllSha256=\"554BF6FF1DE74529BDBCBE97116BD715413BE045DA3161B6807B78B279D1EA31\""
    "$expectedMuUpstreamShaderSha256=\"369AB28A5010E1990BE39EFAA8EA67290807D4FE489EF8AA1DFC0CE8CB4FD38D\""
    "$knownMuOverlayShaderSha256=\"9632EB933D106EEB49C185628654C55AA6EFFFF0A9E7AA66A3F718F12F68F5C5\""
    "$expectedMuRuntimeIniSha256=\"680275EE33D4BC7F59C27A520F098B241B13FA142D0AA92663ECE331B4573CD5\""
    "backups\\deploy_original_mu_2_1_12"
    "Assert-FileSha256Value$muDllPath$expectedMuDllSha256\"MuDynamicNormalMapDLL\""
    "Assert-FileSha256Value$muShaderBackupPath$expectedMuUpstreamShaderSha256\"Backed-upupstreamMushader\""
    "Assert-FileSha256Value$repoMuIniPath$expectedMuRuntimeIniSha256\"MPSMuruntimeprofile\""
    "Copy-Item-LiteralPath$muShaderBackupPath-Destination$muShaderDestinationPath-Force"
    "Copy-Item-LiteralPath$repoMuIniPath-Destination$muIniDestinationPath-Force"
    "Assert-PathWithinRoot$muCompiledShaderPath$muModRoot"
    "Assert-PathWithinRoot$muOverwriteCompiledShaderPath$mo2OverwriteRoot"
    "Remove-Item-LiteralPath$muCompiledShaderPath-Force"
    "Remove-Item-LiteralPath$muOverwriteCompiledShaderPath-Force"
    "Get-FileHash-LiteralPath$SourcePath-AlgorithmSHA256"
    "Get-FileHash-LiteralPath$DestinationPath-AlgorithmSHA256"
    "throw\"SHA-256verificationfailed:"
    "Copy-Item-LiteralPath$sourcePath-Destination$destinationPath-Force"
    "Assert-FileSha256Match$sourcePath$destinationPath"
    "Remove-Item-LiteralPath$conditionPath-Force-ErrorActionSilentlyContinue"
    "$staleMppMuIniPath=Join-Path$modRoot$muRuntimeIniRelativePath"
    "Remove-Item-LiteralPath$staleMppMuIniPath-Force-ErrorActionSilentlyContinue"
    "Textures\RleyBodyMorphs\Details"
    "Textures\RleyBodyMorphs\MuMask"
    "Assert-PathWithinRoot$overlayAssetPath$modRoot"
    "Remove-Item-LiteralPath$overlayAssetPath-Recurse-Force-ErrorActionSilentlyContinue"
)
foreach(required_fragment IN LISTS required_deploy_fragments)
    string(FIND "${compact_deploy}" "${required_fragment}" fragment_offset)
    if(fragment_offset EQUAL -1)
        message(FATAL_ERROR "Deploy script safety fragment is missing: ${required_fragment}")
    endif()
endforeach()

foreach(required_release_symbol_fragment IN ITEMS
    "PRIVATE\"$<$<CONFIG:Release>:/Zi>\""
    "PRIVATE\"$<$<CONFIG:Release>:/DEBUG:FULL>\""
)
    string(FIND "${compact_cmake}" "${required_release_symbol_fragment}" release_symbol_offset)
    if(release_symbol_offset EQUAL -1)
        message(FATAL_ERROR "Release runtime symbol fragment is missing: ${required_release_symbol_fragment}")
    endif()
endforeach()

foreach(required_test IN ITEMS
    "mps_tests"
    "mps_face_route_static_safety"
    "mps_normal_map_bridge_static_safety"
    "mps_racemenu_weight_sync_static_safety"
    "mps_deploy_static_safety"
    "mps_shared_face_morph_static_safety"
    "mps_face_slider_asset_safety"
)
    string(FIND "${compact_deploy}" "\"${required_test}\"" required_test_offset)
    if(required_test_offset EQUAL -1)
        message(FATAL_ERROR "Deploy script does not require test: ${required_test}")
    endif()
endforeach()

set(process_assertion "Assert-DeploymentProcessesStopped")
string(LENGTH "${process_assertion}" process_assertion_length)
string(FIND "${compact_deploy}" "${process_assertion}" first_process_assertion_offset)
if(first_process_assertion_offset EQUAL -1)
    message(FATAL_ERROR "Deploy process assertion function is missing")
endif()
math(EXPR after_first_process_assertion "${first_process_assertion_offset} + ${process_assertion_length}")
string(SUBSTRING "${compact_deploy}" ${after_first_process_assertion} -1 after_process_function)
string(FIND "${after_process_function}" "${process_assertion}" second_process_assertion_relative)
if(second_process_assertion_relative EQUAL -1)
    message(FATAL_ERROR "Deploy process assertion is not called before the build")
endif()
math(EXPR second_process_assertion_offset "${after_first_process_assertion} + ${second_process_assertion_relative}")
math(EXPR after_second_process_assertion "${second_process_assertion_offset} + ${process_assertion_length}")
string(SUBSTRING "${compact_deploy}" ${after_second_process_assertion} -1 after_initial_process_assertion)
string(FIND "${after_initial_process_assertion}" "${process_assertion}" third_process_assertion_relative)
if(third_process_assertion_relative EQUAL -1)
    message(FATAL_ERROR "Deploy process assertion is not repeated before copying")
endif()
math(EXPR third_process_assertion_offset "${after_second_process_assertion} + ${third_process_assertion_relative}")

string(
    FIND
    "${compact_deploy}"
    "&ctest--test-dir$testDirectory--show-only=json-v1"
    test_manifest_offset
)
string(FIND "${compact_deploy}" "if($missingRequiredTests.Count-gt0)" test_manifest_guard_offset)
string(
    FIND
    "${compact_deploy}"
    "&ctest--test-dir$testDirectory--output-on-failure"
    test_offset
)
string(FIND "${compact_deploy}" "if($missingDeploySources.Count-gt0)" preflight_offset)
string(FIND "${compact_deploy}" "New-Item-ItemTypeDirectory" first_directory_write_offset)
string(FIND "${compact_deploy}" "Copy-Item" first_copy_offset)
string(FIND "${compact_deploy}" "Copy-Item\"Scripts\\RleyTextureBridge.pex\"" pex_copy_offset)
string(FIND "${compact_deploy}" "Copy-Item\"interface\\racesex_menu.swf\"" swf_copy_offset)
string(FIND "${compact_deploy}" "Copy-Item\"build\\release-msvc\\ModernPhysiqueSystem.dll\"" dll_copy_offset)
if(
    test_manifest_offset EQUAL -1
    OR test_manifest_guard_offset EQUAL -1
    OR test_offset EQUAL -1
    OR preflight_offset EQUAL -1
    OR first_directory_write_offset EQUAL -1
    OR first_copy_offset EQUAL -1
    OR pex_copy_offset EQUAL -1
    OR swf_copy_offset EQUAL -1
    OR dll_copy_offset EQUAL -1
    OR test_manifest_offset GREATER_EQUAL test_manifest_guard_offset
    OR test_manifest_guard_offset GREATER_EQUAL test_offset
    OR test_offset GREATER_EQUAL preflight_offset
    OR preflight_offset GREATER_EQUAL third_process_assertion_offset
    OR third_process_assertion_offset GREATER_EQUAL first_directory_write_offset
    OR first_directory_write_offset GREATER_EQUAL first_copy_offset
    OR first_copy_offset LESS pex_copy_offset
    OR first_copy_offset GREATER pex_copy_offset
    OR preflight_offset GREATER_EQUAL pex_copy_offset
    OR pex_copy_offset GREATER_EQUAL swf_copy_offset
    OR swf_copy_offset GREATER_EQUAL dll_copy_offset
)
    message(FATAL_ERROR "Deploy must test and preflight before copying PEX, SWF, then DLL")
endif()

string(
    FIND
    "${compact_deploy}"
    "Copy-Item\"build\\debug-msvc\\ModernPhysiqueSystem.dll\""
    debug_dll_copy_offset
)
if(debug_dll_copy_offset GREATER -1)
    message(FATAL_ERROR "Runtime deployment must not copy the ABI-incompatible Debug DLL")
endif()

string(TOLOWER "${compact_deploy}" lowercase_deploy)
foreach(required_asset_directory IN ITEMS
    "New-Item-ItemTypeDirectory-Path(Join-Path$modRoot\"Meshes\\!COR\\Hands\")-Force"
    "New-Item-ItemTypeDirectory-Path(Join-Path$modRoot\"Meshes\\!COR\\Feet\")-Force"
)
    string(FIND "${compact_deploy}" "${required_asset_directory}" asset_directory_offset)
    if(asset_directory_offset EQUAL -1)
        message(FATAL_ERROR "Deploy script does not create an exact female asset directory: ${required_asset_directory}")
    endif()
endforeach()

string(FIND "${compact_deploy}" "$muRestoreAssetRelativePaths" asset_manifest_offset)
string(FIND "${compact_deploy}" "$requiredDeploySources+=Join-Path$mpsMuAssetBackupRoot$assetRelativePath" asset_preflight_offset)
string(FIND "${compact_deploy}" "$missingDeploySources=@(" missing_sources_offset)
if(
    asset_manifest_offset EQUAL -1
    OR asset_preflight_offset EQUAL -1
    OR missing_sources_offset EQUAL -1
    OR asset_manifest_offset GREATER_EQUAL asset_preflight_offset
    OR asset_preflight_offset GREATER_EQUAL missing_sources_offset
)
    message(FATAL_ERROR "MPS Mu assets must join the shared source preflight before deployment writes")
endif()

string(FIND "${compact_deploy}" "$sourcePath=Join-Path$mpsMuAssetBackupRoot$assetRelativePath" asset_source_offset)
string(FIND "${compact_deploy}" "Copy-Item-LiteralPath$sourcePath-Destination$destinationPath-Force" asset_deploy_copy_offset)
string(FIND "${compact_deploy}" "Assert-FileSha256Match$sourcePath$destinationPath" asset_hash_check_offset)
if(
    asset_source_offset EQUAL -1
    OR asset_deploy_copy_offset EQUAL -1
    OR asset_hash_check_offset EQUAL -1
    OR asset_source_offset GREATER_EQUAL asset_deploy_copy_offset
    OR asset_deploy_copy_offset GREATER_EQUAL asset_hash_check_offset
)
    message(FATAL_ERROR "Original female endpoint restore/copy/hash order is unsafe")
endif()

string(FIND "${compact_deploy}" "Assert-FileSha256Value$muDllPath$expectedMuDllSha256" mu_dll_guard_offset)
string(FIND "${compact_deploy}" "Assert-FileSha256Value$muShaderBackupPath$expectedMuUpstreamShaderSha256" mu_source_shader_guard_offset)
string(FIND "${compact_deploy}" "$currentMuShaderHash=Get-FileSha256$muShaderDestinationPath" mu_target_shader_guard_offset)
string(FIND "${compact_deploy}" "Assert-DeploymentProcessesStopped" final_process_guard_offset REVERSE)
string(FIND "${compact_deploy}" "Copy-Item-LiteralPath$muShaderBackupPath-Destination$muShaderDestinationPath-Force" mu_shader_copy_offset)
string(FIND "${compact_deploy}" "Copy-Item-LiteralPath$repoMuIniPath-Destination$muIniDestinationPath-Force" mu_ini_copy_offset)
string(FIND "${compact_deploy}" "Remove-Item-LiteralPath$muCompiledShaderPath-Force" mu_cache_remove_offset)
string(FIND "${compact_deploy}" "Remove-Item-LiteralPath$muOverwriteCompiledShaderPath-Force" mu_overwrite_cache_remove_offset)
if(
    mu_dll_guard_offset EQUAL -1
    OR mu_source_shader_guard_offset EQUAL -1
    OR mu_target_shader_guard_offset EQUAL -1
    OR final_process_guard_offset EQUAL -1
    OR mu_shader_copy_offset EQUAL -1
    OR mu_ini_copy_offset EQUAL -1
    OR mu_cache_remove_offset EQUAL -1
    OR mu_overwrite_cache_remove_offset EQUAL -1
    OR mu_dll_guard_offset GREATER_EQUAL final_process_guard_offset
    OR mu_source_shader_guard_offset GREATER_EQUAL final_process_guard_offset
    OR mu_target_shader_guard_offset GREATER_EQUAL final_process_guard_offset
    OR final_process_guard_offset GREATER_EQUAL mu_shader_copy_offset
    OR mu_shader_copy_offset GREATER_EQUAL mu_ini_copy_offset
    OR mu_ini_copy_offset GREATER_EQUAL mu_cache_remove_offset
    OR mu_cache_remove_offset GREATER_EQUAL mu_overwrite_cache_remove_offset
)
    message(FATAL_ERROR "Upstream Mu runtime restore must be guarded before deployment and invalidate its compiled cache last")
endif()

foreach(forbidden_overlay_deploy IN ITEMS
    "Copy-Item\"SKSE\\Plugins\\MuDynamicNormalMap\\ModernPhysiqueDetailsMale.ini\""
    "Copy-Item\"SKSE\\Plugins\\MuDynamicNormalMap\\ModernPhysiqueDetailsFemale.ini\""
    "Copy-Item-LiteralPath$repoMuShaderPath-Destination$muShaderDestinationPath-Force"
)
    string(FIND "${compact_deploy}" "${forbidden_overlay_deploy}" forbidden_overlay_deploy_offset)
    if(forbidden_overlay_deploy_offset GREATER -1)
        message(FATAL_ERROR "Deploy must not reinstall the isolated Mu overlay layer: ${forbidden_overlay_deploy}")
    endif()
endforeach()

foreach(forbidden_mpp_mu_copy IN ITEMS
    "Copy-Item\"SKSE\\Plugins\\MuDynamicNormalMap.ini\"(Join-Path$modRoot"
    "Copy-Item\"SKSE\\Plugins\\MuDynamicNormalMap\\Shader\\UpdateNormalMap.hlsl\"(Join-Path$modRoot"
)
    string(FIND "${compact_deploy}" "${forbidden_mpp_mu_copy}" forbidden_mpp_mu_copy_offset)
    if(forbidden_mpp_mu_copy_offset GREATER -1)
        message(FATAL_ERROR "The effective Mu INI/shader must be deployed to the higher-priority Mu mod, not MPP")
    endif()
endforeach()

set(mesh_whitelist_source "${lowercase_deploy}")
foreach(allowed_mesh IN ITEMS
    "meshes\\!cor\\hands\\femalehands_1.nif"
    "meshes\\!cor\\feet\\femalefeet_1.nif"
)
    string(FIND "${mesh_whitelist_source}" "${allowed_mesh}" allowed_mesh_offset)
    if(allowed_mesh_offset EQUAL -1)
        message(FATAL_ERROR "Required female endpoint mesh is absent: ${allowed_mesh}")
    endif()
    string(REPLACE "${allowed_mesh}" "" mesh_whitelist_source "${mesh_whitelist_source}")
endforeach()
foreach(allowed_mesh_directory_creation IN ITEMS
    "new-item-itemtypedirectory-path(join-path$modroot\"meshes\\!cor\\hands\")-force|out-null"
    "new-item-itemtypedirectory-path(join-path$modroot\"meshes\\!cor\\feet\")-force|out-null"
)
    string(REPLACE "${allowed_mesh_directory_creation}" "" mesh_whitelist_source "${mesh_whitelist_source}")
endforeach()
string(FIND "${mesh_whitelist_source}" "meshes\\" unexpected_mesh_offset)
if(unexpected_mesh_offset GREATER -1)
    message(FATAL_ERROR "Deploy script references a mesh outside the two-file female endpoint whitelist")
endif()
foreach(forbidden_mesh_fragment IN ITEMS ".tri" "femalebody_0.nif" "femalebody_1.nif" "femalehands_0.nif" "femalefeet_0.nif")
    string(FIND "${lowercase_deploy}" "${forbidden_mesh_fragment}" forbidden_mesh_offset)
    if(forbidden_mesh_offset GREATER -1)
        message(FATAL_ERROR "Deploy script must not copy a TRI/body/zero-weight mesh: ${forbidden_mesh_fragment}")
    endif()
endforeach()

foreach(protected_fragment IN ITEMS "interface\\translations")
    string(FIND "${lowercase_deploy}" "${protected_fragment}" protected_offset)
    if(protected_offset GREATER -1)
        message(FATAL_ERROR "Deploy script targets a protected asset tree: ${protected_fragment}")
    endif()
endforeach()
