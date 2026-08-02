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
    "throw\"CloseModOrganizerandSkyrimbeforedeployment."
    "--show-only=json-v1"
    "if($missingRequiredTests.Count-gt0)"
    "if($missingDeploySources.Count-gt0)"
)
foreach(required_fragment IN LISTS required_deploy_fragments)
    string(FIND "${compact_deploy}" "${required_fragment}" fragment_offset)
    if(fragment_offset EQUAL -1)
        message(FATAL_ERROR "Deploy script safety fragment is missing: ${required_fragment}")
    endif()
endforeach()

foreach(required_test IN ITEMS
    "mps_tests"
    "mps_face_route_static_safety"
    "mps_normal_map_bridge_static_safety"
    "mps_racemenu_weight_sync_static_safety"
    "mps_deploy_static_safety"
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
string(FIND "${compact_deploy}" "Copy-Item\"build\\debug-msvc\\ModernPhysiqueSystem.dll\"" dll_copy_offset)
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

string(TOLOWER "${compact_deploy}" lowercase_deploy)
foreach(protected_fragment IN ITEMS "interface\\translations" "meshes")
    string(FIND "${lowercase_deploy}" "${protected_fragment}" protected_offset)
    if(protected_offset GREATER -1)
        message(FATAL_ERROR "Deploy script targets a protected asset tree: ${protected_fragment}")
    endif()
endforeach()
