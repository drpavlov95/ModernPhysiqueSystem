if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(
    MPS_FACE_ROUTE_SOURCES
    "${MPS_SOURCE_DIR}/src/plugin.cpp"
    "${MPS_SOURCE_DIR}/src/FaceSliderRouter.cpp"
)

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
