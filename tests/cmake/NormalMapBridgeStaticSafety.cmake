if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(MPS_PLUGIN_SOURCE "${MPS_SOURCE_DIR}/src/plugin.cpp")
set(MPS_GLOBALS_HEADER "${MPS_SOURCE_DIR}/src/Globals.h")
set(MPS_BRIDGE_SOURCE "${MPS_SOURCE_DIR}/Source/Scripts/RleyTextureBridge.psc")
set(MPS_BRIDGE_PEX "${MPS_SOURCE_DIR}/Scripts/RleyTextureBridge.pex")

foreach(required_path IN ITEMS
    "${MPS_PLUGIN_SOURCE}"
    "${MPS_GLOBALS_HEADER}"
    "${MPS_BRIDGE_SOURCE}"
    "${MPS_BRIDGE_PEX}"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Normal-map bridge input is missing: ${required_path}")
    endif()
endforeach()

file(READ "${MPS_PLUGIN_SOURCE}" plugin_source)
file(READ "${MPS_GLOBALS_HEADER}" globals_source)
file(READ "${MPS_BRIDGE_SOURCE}" bridge_source)

string(REGEX REPLACE "[ \t\r\n]+" "" compact_plugin "${plugin_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_globals "${globals_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_bridge "${bridge_source}")

set(
    required_dispatch
    "constbooltargetIsFemale=candidate.sex==1;"
)
string(FIND "${compact_plugin}" "${required_dispatch}" dispatch_offset)
if(dispatch_offset EQUAL -1)
    message(FATAL_ERROR "C++ normal-map dispatch no longer derives targetIsFemale from candidate.sex")
endif()

set(
    required_dispatch_call
    "DispatchSelectedNormalPathsStatic(candidate.bodyPath,handNormalPath,targetIsFemale,persist,tracedApplySource)"
)
string(FIND "${compact_plugin}" "${required_dispatch_call}" dispatch_call_offset)
if(dispatch_call_offset EQUAL -1)
    message(FATAL_ERROR "C++ normal-map dispatch no longer forwards targetIsFemale")
endif()

set(
    required_vm_arguments
    "MakeFunctionArguments(std::string(bodyNormalPath),std::string(handNormalPath),isFemale?true:false,persist?true:false,std::string(applySource))"
)
string(FIND "${compact_plugin}" "${required_vm_arguments}" vm_arguments_offset)
if(vm_arguments_offset EQUAL -1)
    message(
        FATAL_ERROR
        "C++ Papyrus arguments must remain body, hand, isFemale, persist, source"
    )
endif()

set(
    required_bridge_name
    "kTextureBridgeApplySelectedNormalPathsForSexFunction=\"ApplySelectedNormalPathsForSex\""
)
string(FIND "${compact_globals}" "${required_bridge_name}" bridge_name_offset)
if(bridge_name_offset EQUAL -1)
    message(FATAL_ERROR "C++ bridge entrypoint is not ApplySelectedNormalPathsForSex")
endif()

set(
    required_bridge_signature
    "FunctionApplySelectedNormalPathsForSex(StringbodyNormalTexturePath,StringhandNormalTexturePath,BoolisFemale,Boolpersist,StringapplySource=\"unspecified\")global"
)
string(FIND "${compact_bridge}" "${required_bridge_signature}" signature_offset)
if(signature_offset EQUAL -1)
    message(FATAL_ERROR "Papyrus sex-aware bridge signature changed unexpectedly")
endif()

set(
    sex_sensitive_ni_override_calls
    "HasOverride"
    "HasSkinOverride"
    "RemoveSkinOverride"
    "RemoveNodeOverride"
    "RemoveOverride"
    "AddOverrideString"
    "AddSkinOverrideString"
)

foreach(function_name IN LISTS sex_sensitive_ni_override_calls)
    set(expected_call "NiOverride.${function_name}(playerRef,isFemale,")
    string(FIND "${compact_bridge}" "${expected_call}" expected_offset)
    if(expected_offset EQUAL -1)
        message(
            FATAL_ERROR
            "Papyrus bridge no longer routes ${function_name} through isFemale"
        )
    endif()

    foreach(hardcoded_sex IN ITEMS True False)
        set(forbidden_call "NiOverride.${function_name}(playerRef,${hardcoded_sex},")
        string(FIND "${compact_bridge}" "${forbidden_call}" forbidden_offset)
        if(NOT forbidden_offset EQUAL -1)
            message(
                FATAL_ERROR
                "Papyrus bridge hardcodes sex in ${function_name}: ${hardcoded_sex}"
            )
        endif()
    endforeach()
endforeach()

# The compiled artifact is part of the mod. Keep it synchronized with the
# source-level entrypoint instead of accepting a DLL/PEX ABI mismatch.
file(READ "${MPS_BRIDGE_PEX}" bridge_pex_hex HEX)
string(HEX "ApplySelectedNormalPathsForSex" required_pex_symbol_hex)
string(FIND "${bridge_pex_hex}" "${required_pex_symbol_hex}" pex_symbol_offset)
if(pex_symbol_offset EQUAL -1)
    message(FATAL_ERROR "Compiled RleyTextureBridge.pex lacks the sex-aware entrypoint")
endif()
