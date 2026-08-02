if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(
    MPS_FACE_ROUTE_SOURCES
    "${MPS_SOURCE_DIR}/src/plugin.cpp"
    "${MPS_SOURCE_DIR}/src/FaceSliderRouter.cpp"
)
set(MPS_FACE_ROUTER_SOURCE "${MPS_SOURCE_DIR}/src/FaceSliderRouter.cpp")
set(MPS_FACE_ROUTER_HEADER "${MPS_SOURCE_DIR}/src/FaceSliderRouter.h")
set(MPS_GLOBALS_HEADER "${MPS_SOURCE_DIR}/src/Globals.h")

set(
    MPS_FORBIDDEN_FACE_ROUTE_CALLS
    "ReloadSliders("
    "SetMember(\"position\""
    "Invoke(\"requestUpdate\""
    "->SetMorphValue("
)

set(
    MPS_FORBIDDEN_PLUGIN_FACE_CALLS
    "->GetMorphValueByName("
)

foreach(source_path IN LISTS MPS_FACE_ROUTE_SOURCES)
    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR "Face-route source is missing: ${source_path}")
    endif()

    file(READ "${source_path}" source_text)
    string(REGEX REPLACE "[ \t\r\n]+" "" compact_source "${source_text}")

    foreach(forbidden_call IN LISTS MPS_FORBIDDEN_FACE_ROUTE_CALLS)
        string(FIND "${compact_source}" "${forbidden_call}" match_offset)
        if(NOT match_offset EQUAL -1)
            message(
                FATAL_ERROR
                "Forbidden face-route call '${forbidden_call}' found in ${source_path}"
            )
        endif()
    endforeach()

    if(source_path STREQUAL "${MPS_SOURCE_DIR}/src/plugin.cpp")
        foreach(forbidden_call IN LISTS MPS_FORBIDDEN_PLUGIN_FACE_CALLS)
            string(FIND "${compact_source}" "${forbidden_call}" match_offset)
            if(NOT match_offset EQUAL -1)
                message(
                    FATAL_ERROR
                    "Unsafe direct face read '${forbidden_call}' found in ${source_path}"
                )
            endif()
        endforeach()
    endif()
endforeach()

foreach(required_path IN ITEMS
    "${MPS_FACE_ROUTER_HEADER}"
    "${MPS_GLOBALS_HEADER}"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Face-route source is missing: ${required_path}")
    endif()
endforeach()

file(READ "${MPS_FACE_ROUTER_SOURCE}" face_router_source)
file(READ "${MPS_FACE_ROUTER_HEADER}" face_router_header)
file(READ "${MPS_SOURCE_DIR}/src/plugin.cpp" plugin_source)
file(READ "${MPS_GLOBALS_HEADER}" globals_source)
string(REGEX REPLACE "[ \t\r\n]+" "" compact_face_router "${face_router_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_face_router_header "${face_router_header}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_plugin "${plugin_source}")
string(REGEX REPLACE "[ \t\r\n]+" "" compact_globals "${globals_source}")

# The supported RaceMenu 0.4.16 binary has a display string version of
# 4.0.0.0 but a VS_FIXEDFILEINFO value of 3.4.5.0. The native gate reads the
# fixed block, so pin that exact value alongside the stronger hash/PE/signature
# checks. This guards against silently reintroducing the all-face-disabled bug.
foreach(required_fixed_version_fragment IN ITEMS
    "kExpectedSkeeFixedVersionMajor=3"
    "kExpectedSkeeFixedVersionMinor=4"
    "kExpectedSkeeFixedVersionBuild=5"
    "kExpectedSkeeFixedVersionPrivate=0"
    "TryReadFixedFileVersion(modulePath,actualVersion)"
    "version.major==kExpectedSkeeFixedVersionMajor"
    "version.minor==kExpectedSkeeFixedVersionMinor"
    "version.build==kExpectedSkeeFixedVersionBuild"
    "version.privatePart==kExpectedSkeeFixedVersionPrivate"
    "!MatchesExpectedFixedFileVersion(actualVersion)"
    "reason=fixed_file_version_mismatch"
)
    string(
        FIND
        "${compact_face_router}"
        "${required_fixed_version_fragment}"
        fixed_version_offset
    )
    if(fixed_version_offset EQUAL -1)
        message(
            FATAL_ERROR
            "FaceRouter exact SKEE fixed-version gate fragment is missing: ${required_fixed_version_fragment}"
        )
    endif()
endforeach()

# A null route-specific CreateSliderList result must not permanently poison
# the already-certified ABI shim. Only an SEH fault can latch it unavailable.
string(
    FIND
    "${compact_face_router}"
    "boolFaceSliderRouter::TryExactRepair("
    try_exact_repair_offset
)
string(
    FIND
    "${compact_face_router}"
    "boolFaceSliderRouter::TryReadAxisValues("
    try_read_axis_offset
)
if(
    try_exact_repair_offset EQUAL -1
    OR try_read_axis_offset LESS_EQUAL try_exact_repair_offset
)
    message(FATAL_ERROR "FaceRouter TryExactRepair boundary is missing")
endif()
math(
    EXPR
    try_exact_repair_length
    "${try_read_axis_offset} - ${try_exact_repair_offset}"
)
string(
    SUBSTRING
    "${compact_face_router}"
    ${try_exact_repair_offset}
    ${try_exact_repair_length}
    try_exact_repair_source
)
string(FIND "${try_exact_repair_source}" "if(!result){" null_result_offset)
string(
    REGEX MATCHALL
    "repairGateState_=GateState::Unavailable"
    repair_poison_matches
    "${try_exact_repair_source}"
)
string(
    REGEX MATCHALL
    "createSliderList_=nullptr"
    function_poison_matches
    "${try_exact_repair_source}"
)
list(LENGTH repair_poison_matches repair_poison_count)
list(LENGTH function_poison_matches function_poison_count)
if(
    null_result_offset EQUAL -1
    OR NOT repair_poison_count EQUAL 1
    OR NOT function_poison_count EQUAL 1
)
    message(
        FATAL_ERROR
        "Only the CreateSliderList SEH-fault branch may disable the ABI shim"
    )
endif()

# File metadata/path reads can fail transiently under a virtualized MO2 path.
# They must leave the gate Unchecked so another SetSliders boundary can retry;
# exact identity mismatches remain permanently unavailable.
string(
    FIND
    "${compact_face_router}"
    "boolFaceSliderRouter::PrepareExactRepair()"
    prepare_exact_repair_offset
)
if(
    prepare_exact_repair_offset EQUAL -1
    OR try_exact_repair_offset LESS_EQUAL prepare_exact_repair_offset
)
    message(FATAL_ERROR "FaceRouter PrepareExactRepair boundary is missing")
endif()
math(
    EXPR
    prepare_exact_repair_length
    "${try_exact_repair_offset} - ${prepare_exact_repair_offset}"
)
string(
    SUBSTRING
    "${compact_face_router}"
    ${prepare_exact_repair_offset}
    ${prepare_exact_repair_length}
    prepare_exact_repair_source
)
string(
    FIND
    "${prepare_exact_repair_source}"
    "TryReadFixedFileVersion(modulePath,actualVersion)"
    fixed_version_read_offset
)
string(
    FIND
    "${prepare_exact_repair_source}"
    "!MatchesExpectedFixedFileVersion(actualVersion)"
    fixed_version_match_offset
)
string(
    FIND
    "${prepare_exact_repair_source}"
    "ComputeSha256(modulePath,actualHash)"
    fixed_version_hash_offset
)
string(
    FIND
    "${prepare_exact_repair_source}"
    "repairGateState_=GateState::Available"
    repair_available_offset
)
if(
    fixed_version_read_offset EQUAL -1
    OR fixed_version_match_offset LESS_EQUAL fixed_version_read_offset
    OR fixed_version_hash_offset LESS_EQUAL fixed_version_match_offset
    OR repair_available_offset LESS_EQUAL fixed_version_hash_offset
)
    message(
        FATAL_ERROR
        "FaceRouter must validate the fixed file version before hashing and enabling the shim"
    )
endif()
foreach(transient_reason IN ITEMS
    "reason=module_path_unavailable"
    "reason=fixed_file_version_unreadable"
    "reason=sha256_unreadable"
)
    string(
        FIND
        "${prepare_exact_repair_source}"
        "${transient_reason}"
        transient_reason_offset
    )
    if(transient_reason_offset EQUAL -1)
        message(
            FATAL_ERROR
            "FaceRouter transient exact-gate reason is missing: ${transient_reason}"
        )
    endif()
endforeach()
string(
    FIND
    "${prepare_exact_repair_source}"
    "repairGateState_=GateState::Unavailable;if(!PrepareReadbackAbi()"
    eager_repair_poison_offset
)
if(NOT eager_repair_poison_offset EQUAL -1)
    message(
        FATAL_ERROR
        "FaceRouter must not eagerly latch transient exact-gate failures unavailable"
    )
endif()
string(
    REGEX MATCHALL
    "repairGateState_=GateState::Unavailable"
    permanent_repair_poison_matches
    "${prepare_exact_repair_source}"
)
list(LENGTH permanent_repair_poison_matches permanent_repair_poison_count)
if(NOT permanent_repair_poison_count EQUAL 4)
    message(
        FATAL_ERROR
        "FaceRouter exact repair gate must latch only its four permanent ABI/identity failures"
    )
endif()

# The current route is discovered from the UI descriptors produced after the
# current-sex SKEE map is prepared. Never restore the old per-sex arrays or the
# sentinel/subType probe that tried to prove a map by reading the same map.
set(
    legacy_face_route_identifiers
    "kExpectedMaleIndices"
    "kExpectedFemaleIndices"
    "kSentinelIndex"
    "ProbeInternalMapSex"
)
set(
    combined_face_router_source
    "${compact_face_router}${compact_face_router_header}"
)
foreach(legacy_identifier IN LISTS legacy_face_route_identifiers)
    string(
        FIND
        "${combined_face_router_source}"
        "${legacy_identifier}"
        legacy_identifier_offset
    )
    if(NOT legacy_identifier_offset EQUAL -1)
        message(
            FATAL_ERROR
            "Legacy/circular face-route inference '${legacy_identifier}' must not return"
        )
    endif()
endforeach()

foreach(legacy_index IN ITEMS 379 378 377 358 357 356 215 217 458)
    string(
        REGEX MATCH
        "(^|[^0-9])${legacy_index}([^0-9]|$)"
        legacy_index_match
        "${compact_face_router}"
    )
    if(legacy_index_match)
        message(
            FATAL_ERROR
            "Legacy hardcoded face-route index ${legacy_index} must not return to FaceSliderRouter"
        )
    endif()
endforeach()

# PrepareMapForCurrentSex may publish a prepared key only after the exact SKEE
# repair succeeds. The Scaleform return value carries that failure back to
# SetSliders, where only extended face sliders are disabled.
string(
    FIND
    "${compact_face_router}"
    "FaceSliderRouter::PrepareMapForCurrentSex("
    prepare_map_offset
)
string(
    FIND
    "${compact_face_router}"
    "FaceSliderRouter::SubmitTarget("
    submit_target_offset
)
if(prepare_map_offset EQUAL -1 OR submit_target_offset LESS_EQUAL prepare_map_offset)
    message(FATAL_ERROR "FaceRouter PrepareMapForCurrentSex boundary is missing")
endif()
math(EXPR prepare_map_length "${submit_target_offset} - ${prepare_map_offset}")
string(
    SUBSTRING
    "${compact_face_router}"
    ${prepare_map_offset}
    ${prepare_map_length}
    prepare_map_source
)
string(FIND "${prepare_map_source}" "TryExactRepair(race,sex)" prepare_repair_offset)
string(FIND "${prepare_map_source}" "preparedMapKey_=key;" prepared_key_offset)
if(
    prepare_repair_offset EQUAL -1
    OR prepared_key_offset EQUAL -1
    OR prepare_repair_offset GREATER_EQUAL prepared_key_offset
)
    message(
        FATAL_ERROR
        "FaceRouter must complete exact repair before publishing the current-sex prepared key"
    )
endif()
math(EXPR prepare_failure_length "${prepared_key_offset} - ${prepare_repair_offset}")
string(
    SUBSTRING
    "${prepare_map_source}"
    ${prepare_repair_offset}
    ${prepare_failure_length}
    prepare_failure_source
)
string(FIND "${prepare_failure_source}" "preparedMapKey_.reset();" prepare_reset_offset)
string(FIND "${prepare_failure_source}" "returnfalse;" prepare_false_offset)
if(prepare_reset_offset EQUAL -1 OR prepare_false_offset EQUAL -1)
    message(
        FATAL_ERROR
        "FaceRouter exact-repair failure must clear the prepared key and fail closed"
    )
endif()

set(
    required_prepare_bridge_fragments
    "classPrepareFaceMapForCurrentSexHandler:publicRE::GFxFunctionHandler"
    "ready=g_faceSliderRouter.PrepareMapForCurrentSex("
    "a_params.retVal->SetBoolean(ready);"
    "newPrepareFaceMapForCurrentSexHandler()"
    "a_root->SetMember(kScaleformPrepareFaceMapFunction,prepareFaceMapCallback)"
)
foreach(required_fragment IN LISTS required_prepare_bridge_fragments)
    string(FIND "${compact_plugin}" "${required_fragment}" bridge_fragment_offset)
    if(bridge_fragment_offset EQUAL -1)
        message(
            FATAL_ERROR
            "Current-sex face-map Scaleform bridge fragment is missing: ${required_fragment}"
        )
    endif()
endforeach()

string(
    FIND
    "${compact_plugin}"
    "classPrepareFaceMapForCurrentSexHandler"
    prepare_handler_offset
)
string(
    FIND
    "${compact_plugin}"
    "classSetWeightSyncHandler"
    set_weight_handler_offset
)
if(
    prepare_handler_offset EQUAL -1
    OR set_weight_handler_offset LESS_EQUAL prepare_handler_offset
)
    message(FATAL_ERROR "PrepareFaceMapForCurrentSex handler boundary is missing")
endif()
math(EXPR prepare_handler_length "${set_weight_handler_offset} - ${prepare_handler_offset}")
string(
    SUBSTRING
    "${compact_plugin}"
    ${prepare_handler_offset}
    ${prepare_handler_length}
    prepare_handler_source
)
string(
    FIND
    "${prepare_handler_source}"
    "TryAcquireRaceMenuBodyMorphInterface"
    synchronous_exchange_offset
)
if(NOT synchronous_exchange_offset EQUAL -1)
    message(
        FATAL_ERROR
        "SetSliders preflight must not perform synchronous RaceMenu interface exchange"
    )
endif()
foreach(required_handler_fragment IN ITEMS
    "constboolready=g_faceSliderRouter.PrepareMapForCurrentSex("
    "static_cast<RE::GFxMovieView*>(a_params.movie)"
    "static_cast<mps::FaceSex>(rawSex)"
)
    string(
        FIND
        "${prepare_handler_source}"
        "${required_handler_fragment}"
        handler_fragment_offset
    )
    if(handler_fragment_offset EQUAL -1)
        message(
            FATAL_ERROR
            "SetSliders preflight handler fragment is missing: ${required_handler_fragment}"
        )
    endif()
endforeach()
string(
    FIND
    "${compact_globals}"
    "kScaleformPrepareFaceMapFunction=\"PrepareFaceMapForCurrentSex\";"
    prepare_bridge_name_offset
)
if(prepare_bridge_name_offset EQUAL -1)
    message(FATAL_ERROR "PrepareFaceMapForCurrentSex Scaleform bridge name is missing")
endif()

# BuildSnapshot may discover dynamic extended indices, but it must not consult
# subType before the map has been prepared: doing so recreates the circular
# inference that caused male/female routes to validate the wrong global map.
string(
    FIND
    "${compact_face_router}"
    "FaceSliderRouter::BuildSnapshot("
    build_snapshot_offset
)
string(
    FIND
    "${compact_face_router}"
    "FaceSliderRouter::ValidatePreparedAxes("
    validate_axes_definition_offset
)
if(
    build_snapshot_offset EQUAL -1
    OR validate_axes_definition_offset LESS_EQUAL build_snapshot_offset
)
    message(FATAL_ERROR "FaceRouter BuildSnapshot/ValidatePreparedAxes boundary is missing")
endif()
math(EXPR build_snapshot_length "${validate_axes_definition_offset} - ${build_snapshot_offset}")
string(
    SUBSTRING
    "${compact_face_router}"
    ${build_snapshot_offset}
    ${build_snapshot_length}
    build_snapshot_source
)
string(FIND "${build_snapshot_source}" "subType" premature_subtype_offset)
if(NOT premature_subtype_offset EQUAL -1)
    message(
        FATAL_ERROR
        "FaceRouter BuildSnapshot must not infer current-sex map alignment from subType"
    )
endif()
foreach(required_dynamic_route_fragment IN ITEMS
    "nativeIndex<AxisRoute::kFirstExtendedIndex"
    ".extendedIndex=nativeIndex"
)
    string(
        FIND
        "${build_snapshot_source}"
        "${required_dynamic_route_fragment}"
        dynamic_route_offset
    )
    if(dynamic_route_offset EQUAL -1)
        message(
            FATAL_ERROR
            "FaceRouter dynamic route discovery fragment is missing: ${required_dynamic_route_fragment}"
        )
    endif()
endforeach()

string(
    FIND
    "${compact_face_router}"
    "FaceSliderRouter::ResolveCharGen("
    resolve_chargen_offset
)
if(resolve_chargen_offset LESS_EQUAL validate_axes_definition_offset)
    message(FATAL_ERROR "FaceRouter ValidatePreparedAxes boundary is missing")
endif()
math(EXPR validate_axes_length "${resolve_chargen_offset} - ${validate_axes_definition_offset}")
string(
    SUBSTRING
    "${compact_face_router}"
    ${validate_axes_definition_offset}
    ${validate_axes_length}
    validate_axes_source
)
foreach(required_post_prepare_fragment IN ITEMS
    "\"subType\",subType"
    "nativeIndex!=route.extendedIndex"
    "subType!=0"
)
    string(
        FIND
        "${validate_axes_source}"
        "${required_post_prepare_fragment}"
        post_prepare_offset
    )
    if(post_prepare_offset EQUAL -1)
        message(
            FATAL_ERROR
            "FaceRouter post-prepare axis validation fragment is missing: ${required_post_prepare_fragment}"
        )
    endif()
endforeach()

# Tick must require the successful SetSliders proof before observing a route,
# then repeat the exact repair immediately before post-prepare validation. The
# second repair prevents a key-only preflight proof from aging if SKEE's
# process-global race map is replaced during list settling.
string(FIND "${compact_face_router}" "FaceSliderRouter::Tick(" tick_offset)
string(FIND "${compact_face_router}" "FaceSliderRouter::NeedsTick()" needs_tick_offset)
if(tick_offset EQUAL -1 OR needs_tick_offset LESS_EQUAL tick_offset)
    message(FATAL_ERROR "FaceRouter Tick/NeedsTick boundary is missing")
endif()
math(EXPR tick_length "${needs_tick_offset} - ${tick_offset}")
string(SUBSTRING "${compact_face_router}" ${tick_offset} ${tick_length} tick_source)
string(
    FIND
    "${tick_source}"
    "preflightProofValid_.load("
    preflight_gate_offset
)
string(
    FIND
    "${tick_source}"
    "TryExactRepair(frame.race,frame.sex)"
    certification_repair_offset
)
string(
    FIND
    "${tick_source}"
    "ValidatePreparedAxes("
    tick_validate_offset
)
if(
    preflight_gate_offset EQUAL -1
    OR certification_repair_offset LESS_EQUAL preflight_gate_offset
    OR tick_validate_offset LESS_EQUAL certification_repair_offset
)
    message(
        FATAL_ERROR
        "FaceRouter must require SetSliders preflight and repeat exact repair before axis validation"
    )
endif()

foreach(required_fail_closed_fragment IN ITEMS
    "successful_setsliders_preflight_missing_for_route"
    "*preparedMapKey_!=key"
    "preparedMapMovie_!=frame.movie"
    "TryGetBoolMember(entry,\"enabled\",enabled)"
    "!enabled"
    "controller_.Observe(key,RouteSnapshot{})"
)
    string(
        FIND
        "${tick_source}${build_snapshot_source}"
        "${required_fail_closed_fragment}"
        fail_closed_offset
    )
    if(fail_closed_offset EQUAL -1)
        message(
            FATAL_ERROR
            "FaceRouter fail-closed fragment is missing: ${required_fail_closed_fragment}"
        )
    endif()
endforeach()

foreach(required_cross_thread_fragment IN ITEMS
    "preflightProofValid_.store(false,std::memory_order_release);if(!CheckThread(\"PrepareMapForCurrentSex\",true))"
    "preparedMapMovie_=movie"
    "preflightProofValid_.store(true,std::memory_order_release)"
)
    string(
        FIND
        "${prepare_map_source}"
        "${required_cross_thread_fragment}"
        cross_thread_offset
    )
    if(cross_thread_offset EQUAL -1)
        message(
            FATAL_ERROR
            "FaceRouter preflight proof lifecycle fragment is missing: ${required_cross_thread_fragment}"
        )
    endif()
endforeach()
