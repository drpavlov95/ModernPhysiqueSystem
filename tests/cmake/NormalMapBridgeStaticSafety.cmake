if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(MPS_PLUGIN "${MPS_SOURCE_DIR}/src/plugin.cpp")
set(MPS_GLOBALS "${MPS_SOURCE_DIR}/src/Globals.h")
set(MPS_MU_NATIVE_API "${MPS_SOURCE_DIR}/src/MuDynamicNormalMapAPI.h")
set(MPS_UI_HTML "${MPS_SOURCE_DIR}/PrismaUI/views/RleyBodyMorphs/index.html")
set(MPS_UI_JS "${MPS_SOURCE_DIR}/PrismaUI/views/RleyBodyMorphs/script.js")
set(MPS_CONFIG "${MPS_SOURCE_DIR}/SKSE/Plugins/PhysiqueWheel.ini")
set(MPS_MU_CONFIG "${MPS_SOURCE_DIR}/SKSE/Plugins/MuDynamicNormalMap.ini")
set(MPS_CMAKE "${MPS_SOURCE_DIR}/CMakeLists.txt")

foreach(required_file IN ITEMS
    "${MPS_PLUGIN}"
    "${MPS_GLOBALS}"
    "${MPS_MU_NATIVE_API}"
    "${MPS_UI_HTML}"
    "${MPS_UI_JS}"
    "${MPS_CONFIG}"
    "${MPS_MU_CONFIG}"
    "${MPS_CMAKE}"
)
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Required normal-map integration file is missing: ${required_file}")
    endif()
endforeach()

file(READ "${MPS_PLUGIN}" plugin_source)
file(READ "${MPS_GLOBALS}" globals_source)
file(READ "${MPS_MU_NATIVE_API}" mu_native_api_source)
file(READ "${MPS_UI_HTML}" ui_html_source)
file(READ "${MPS_UI_JS}" ui_js_source)
file(READ "${MPS_CONFIG}" config_source)
file(READ "${MPS_MU_CONFIG}" mu_config_source)
file(READ "${MPS_CMAKE}" cmake_source)

set(runtime_source
    "${plugin_source}\n${globals_source}\n${mu_native_api_source}\n${ui_html_source}\n${ui_js_source}\n${config_source}\n${cmake_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_runtime "${runtime_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_mu_config "${mu_config_source}")

foreach(required_fragment IN ITEMS
    "kMuSkinSlotBits=0x0000018D"
    "Backend=auto"
    "kType=0x4D444E4D"
    "api->QUpdateNormalmap(player,slotBits)"
    "\"MuDynamicNormalMap\""
    "DispatchMuNormalUpdateStatic(slotBits,lifecycle.source)"
    "RealtimePreview=1"
    "MORPH_PREVIEW_INTERVAL=64"
)
    string(FIND "${compact_runtime}" "${required_fragment}" required_offset)
    if(required_offset EQUAL -1)
        message(FATAL_ERROR "Required base Mu integration fragment is missing: ${required_fragment}")
    endif()
endforeach()

foreach(forbidden_normal_map_fragment IN ITEMS
    "Backend=legacy"
    "SelectionMode=legacy"
    "StableHysteresisPercent"
)
    string(FIND "${compact_runtime}" "${forbidden_normal_map_fragment}" forbidden_normal_map_offset)
    if(forbidden_normal_map_offset GREATER -1)
        message(FATAL_ERROR "Removed pre-generated normal-map backend residue remains: ${forbidden_normal_map_fragment}")
    endif()
endforeach()

foreach(forbidden_fragment IN ITEMS
    "DetailsOverlay"
    "detailsOverlay"
    "onDetailsOverlay"
    "OverlayEnabled"
    "StageMuDetailsOverlay"
    "AcknowledgeMuDetailsOverlay"
    "QueueStagedMuNormalUpdate"
    "ReportMuDetailsOverlaySetResult"
    "MuNormalCoordinator"
    "MuDynamicNormalMap.SetNormalMap("
    "RleyBodyMorphs\\Details"
    "RleyBodyMorphs\\MuMask"
)
    string(FIND "${compact_runtime}" "${forbidden_fragment}" forbidden_offset)
    if(forbidden_offset GREATER -1)
        message(FATAL_ERROR "Overlay runtime residue remains: ${forbidden_fragment}")
    endif()
endforeach()

foreach(required_mu_setting IN ITEMS
    "GPUForceSync=false"
    "AutoTaskQ=0"
    "TaskQTickMS=0"
    "RealtimeDetect=false"
    "logLevel=error"
)
    string(FIND "${compact_mu_config}" "${required_mu_setting}" setting_offset)
    if(setting_offset EQUAL -1)
        message(FATAL_ERROR "Realtime Mu setting is missing: ${required_mu_setting}")
    endif()
endforeach()

foreach(forbidden_overlay_file IN ITEMS
    "${MPS_SOURCE_DIR}/SKSE/Plugins/MuDynamicNormalMap/ModernPhysiqueDetailsMale.ini"
    "${MPS_SOURCE_DIR}/SKSE/Plugins/MuDynamicNormalMap/ModernPhysiqueDetailsFemale.ini"
    "${MPS_SOURCE_DIR}/SKSE/Plugins/MuDynamicNormalMap/Shader/UpdateNormalMap.hlsl"
)
    if(EXISTS "${forbidden_overlay_file}")
        message(FATAL_ERROR "Overlay runtime file must not be shipped: ${forbidden_overlay_file}")
    endif()
endforeach()

# The Papyrus normal-map bridge was removed: normal maps are driven natively
# (the Mu interface exchange), so no script, no ESP and no Papyrus Extender is
# part of this mod. Fail loudly if any of it creeps back in.
foreach(forbidden_bridge_fragment IN ITEMS
    "RleyTextureBridge"
    "ClearLegacyOverridesOnLoad"
    "PO3_SKSEFunctions"
)
    string(FIND "${compact_runtime}" "${forbidden_bridge_fragment}" forbidden_bridge_offset)
    if(forbidden_bridge_offset GREATER -1)
        message(FATAL_ERROR "Removed Papyrus bridge residue remains: ${forbidden_bridge_fragment}")
    endif()
endforeach()

foreach(forbidden_script_file IN ITEMS
    "${MPS_SOURCE_DIR}/Scripts/RleyTextureBridge.pex"
    "${MPS_SOURCE_DIR}/Source/Scripts/RleyTextureBridge.psc"
    "${MPS_SOURCE_DIR}/RleyBodyMorphs.esp"
)
    if(EXISTS "${forbidden_script_file}")
        message(FATAL_ERROR "Removed Papyrus bridge file is back: ${forbidden_script_file}")
    endif()
endforeach()
