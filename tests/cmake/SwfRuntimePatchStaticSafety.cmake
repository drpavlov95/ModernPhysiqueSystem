if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(plugin_path "${MPS_SOURCE_DIR}/src/plugin.cpp")
set(patcher_path "${MPS_SOURCE_DIR}/src/SwfStockPatcher.cpp")
set(source_list_path "${MPS_SOURCE_DIR}/cmake/sourcelist.cmake")
set(recipe_path "${MPS_SOURCE_DIR}/resources/racemenu-0.4.16-to-physique-wheel.mpspatch")

file(READ "${plugin_path}" plugin_source)
file(READ "${patcher_path}" patcher_source)
file(READ "${source_list_path}" source_list)
file(SHA256 "${recipe_path}" recipe_sha256)

if(NOT recipe_sha256 STREQUAL "3902897ed29bdbe44b5bfebcd3971f8d4b39386cb58ea3acf742b6f6a8bdfed9")
    message(FATAL_ERROR "Embedded RaceMenu MPS1 recipe changed unexpectedly: ${recipe_sha256}")
endif()

foreach(required_fragment IN ITEMS
    "tasks->AddTask([]() { PatchRaceMenuSwfOnDataLoaded(); })"
    "stream.stream->totalSize"
    "stream.stream->DoRead(outBytes.data(), totalSize, bytesRead)"
    "MoveFileExW("
    "MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH"
    "ReconstructPatchedSwf(stockBytes, patchData)"
    "src/core/SwfPatcher.cpp"
)
    string(FIND "${plugin_source}${patcher_source}${source_list}" "${required_fragment}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "Required runtime SWF patch safety fragment is missing: ${required_fragment}")
    endif()
endforeach()

string(FIND "${patcher_source}" "stream.stream->DoOpen()" duplicate_open)
if(NOT duplicate_open EQUAL -1)
    message(FATAL_ERROR "Runtime SWF reader must not reopen the resource stream created by BSResourceNiBinaryStream")
endif()

string(FIND "${patcher_source}" "stream.get_info(" obsolete_get_info)
if(NOT obsolete_get_info EQUAL -1)
    message(FATAL_ERROR "Runtime SWF reader regressed to CommonLib's empty get_info() stub")
endif()

string(FIND "${patcher_source}" "std::filesystem::rename(" obsolete_rename)
if(NOT obsolete_rename EQUAL -1)
    message(FATAL_ERROR "Runtime SWF writer regressed to non-replacing std::filesystem::rename")
endif()

string(FIND "${plugin_source}" "SKSEPluginLoad(const SKSE::LoadInterface* skse)" load_offset)
if(load_offset EQUAL -1)
    message(FATAL_ERROR "SKSEPluginLoad not found")
endif()
string(SUBSTRING "${plugin_source}" ${load_offset} -1 load_tail)
string(FIND "${load_tail}" "PatchStockSwfIfNeeded(" patch_in_load)
if(NOT patch_in_load EQUAL -1)
    message(FATAL_ERROR "Runtime SWF patching must not run synchronously from SKSEPluginLoad")
endif()
