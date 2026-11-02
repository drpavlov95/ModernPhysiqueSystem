if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(MPS_SHARED_FACE_SOURCE "${MPS_SOURCE_DIR}/src/SharedFaceMorphLibrary.cpp")
set(MPS_PLUGIN_SOURCE "${MPS_SOURCE_DIR}/src/plugin.cpp")
foreach(required_path IN ITEMS "${MPS_SHARED_FACE_SOURCE}" "${MPS_PLUGIN_SOURCE}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Shared-face safety input is missing: ${required_path}")
    endif()
endforeach()

file(READ "${MPS_SHARED_FACE_SOURCE}" shared_face_source)
file(READ "${MPS_PLUGIN_SOURCE}" plugin_source)
string(REGEX REPLACE "[ \t\r\n]+" "" compact_shared_face "${shared_face_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_plugin "${plugin_source}")

foreach(required_fragment IN ITEMS
    "std::array<SexHistory,2>sexHistory"
    "std::array<std::unordered_map<std::string,GeometryHistory>,2>geometryHistory"
    "liveBuffers.find(key)"
    "liveIt->second->vertexData==state.data"
    "Fingerprint(state.data,state.count,true)==state.lastAppliedFingerprint"
    "voidSharedFaceMorphLibrary::ForgetHistory()"
    "kMaxGeometryHistoryPerSex=32"
    "PruneGeometryHistory(sexIndex)"
    "CentredMaxDistance(fod->vertexData,entry.tri.base)"
    "IsSafeFaceGeometryCandidate({"
    "IsMaterialFaceResidualImprovement("
)
    string(FIND "${compact_shared_face}" "${required_fragment}" fragment_offset)
    if(fragment_offset EQUAL -1)
        message(FATAL_ERROR "Shared-face safety fragment is missing: ${required_fragment}")
    endif()
endforeach()

foreach(required_fragment IN ITEMS
    "if(a_event->strArg==\"ChangeRace\"){"
    "caseSKSE::MessagingInterface::kPreLoadGame:"
    "caseSKSE::MessagingInterface::kPostLoadGame:"
    "caseSKSE::MessagingInterface::kNewGame:"
)
    string(FIND "${compact_plugin}" "${required_fragment}" fragment_offset)
    if(fragment_offset EQUAL -1)
        message(FATAL_ERROR "Shared-face lifecycle fragment is missing: ${required_fragment}")
    endif()
endforeach()

string(REGEX MATCHALL "g_sharedFaceMorphLibrary.ForgetHistory\(\)" forget_calls "${compact_plugin}")
list(LENGTH forget_calls forget_call_count)
if(forget_call_count LESS 5)
    message(FATAL_ERROR "Shared-face history must be cleared on race and save/player boundaries")
endif()

foreach(required_recovery_fragment IN ITEMS
    "classNotifyFaceCorruptionRecoveryHandler:publicRE::GFxFunctionHandler"
    "BeginFaceMorphTransition(\"impossible_native_slider_recovery\",false)"
    "newNotifyFaceCorruptionRecoveryHandler()"
)
    string(FIND "${compact_plugin}" "${required_recovery_fragment}" recovery_offset)
    if(recovery_offset EQUAL -1)
        message(FATAL_ERROR "Shared-face contamination recovery fragment is missing: ${required_recovery_fragment}")
    endif()
endforeach()
