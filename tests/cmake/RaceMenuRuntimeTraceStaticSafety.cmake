if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(MPS_PLUGIN_SOURCE "${MPS_SOURCE_DIR}/src/plugin.cpp")
set(
    MPS_LIFECYCLE_HEADER
    "${MPS_SOURCE_DIR}/src/core/RaceMenuLifecycleState.h"
)
set(MPS_CONFIG "${MPS_SOURCE_DIR}/config/ModernPhysiqueSystem.ini")

foreach(required_path IN ITEMS
    "${MPS_PLUGIN_SOURCE}"
    "${MPS_LIFECYCLE_HEADER}"
    "${MPS_CONFIG}"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "RaceMenu runtime trace input is missing: ${required_path}")
    endif()
endforeach()

file(READ "${MPS_PLUGIN_SOURCE}" plugin_source)
file(READ "${MPS_LIFECYCLE_HEADER}" lifecycle_header)
file(READ "${MPS_CONFIG}" config_source)

foreach(required_plugin_fragment IN ITEMS
    "LogRaceMenuSequence(\"MENU_OPEN_EVENT\")"
    "LogRaceMenuSequence(\"MENU_CLOSE_EVENT\")"
    "\"PREPARE_CURRENT_SEX_ENTER\""
    "\"PREPARE_CURRENT_SEX_RESULT\""
    "\"PRE_CHANGE_SEX_ENTER\""
    "\"PRE_CHANGE_SEX_RESULT\""
    "\"RSM_EVENT_ENTER\""
    "\"RSM_TRANSITION_EVENT\""
    "\"RSM_REBUILD_OBSERVED\""
    "LogRaceMenuSequence(\"REBUILD_GATE_TIMEOUT\")"
    "LogRaceMenuSequence(\"ACTOR_SEX_CHANGE_OBSERVED\")"
)
    string(FIND "${plugin_source}" "${required_plugin_fragment}" fragment_offset)
    if(fragment_offset EQUAL -1)
        message(
            FATAL_ERROR
            "RaceMenu runtime trace boundary is missing: ${required_plugin_fragment}"
        )
    endif()
endforeach()

foreach(required_lifecycle_fragment IN ITEMS
    "BeginSynchronousTransition"
    "ObserveTransitionEvent"
    "ObserveRebuild"
    "CancelTransition"
    "RebuildTimedOut"
    "menuGeneration"
    "rebuildGeneration"
)
    string(
        FIND
        "${lifecycle_header}"
        "${required_lifecycle_fragment}"
        lifecycle_fragment_offset
    )
    if(lifecycle_fragment_offset EQUAL -1)
        message(
            FATAL_ERROR
            "RaceMenu lifecycle correlation field is missing: ${required_lifecycle_fragment}"
        )
    endif()
endforeach()

# The sequence trace is intentionally diagnostic-only. Require the opt-in
# guard in the local source window immediately preceding its first lifecycle
# mutation. This remains stable across CRLF checkout policy.
string(
    FIND
    "${plugin_source}"
    "const auto lifecycle = g_raceMenuLifecycleState.NextSnapshot();"
    trace_snapshot_offset
)
if(trace_snapshot_offset LESS 500)
    message(FATAL_ERROR "RaceMenu sequence trace snapshot boundary is missing")
endif()
math(EXPR trace_guard_window_start "${trace_snapshot_offset} - 500")
string(
    SUBSTRING
    "${plugin_source}"
    ${trace_guard_window_start}
    500
    trace_guard_window
)
string(
    FIND
    "${trace_guard_window}"
    "if (!FaceMorphDiagnostics::IsCausalTraceEnabled())"
    trace_guard_offset
)
if(trace_guard_offset EQUAL -1)
    message(FATAL_ERROR "RaceMenu sequence tracing must remain opt-in")
endif()

string(FIND "${config_source}" "FaceCausalTrace=0" trace_default_offset)
if(trace_default_offset EQUAL -1)
    message(FATAL_ERROR "RaceMenu causal tracing must remain disabled by default")
endif()
