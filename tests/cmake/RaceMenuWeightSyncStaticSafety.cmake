if(NOT DEFINED MPS_SOURCE_DIR)
    message(FATAL_ERROR "MPS_SOURCE_DIR is required")
endif()

set(
    MPS_RACEMENU_SOURCE
    "${MPS_SOURCE_DIR}/reference/RaceMenu/patched/RaceMenu.as"
)
set(MPS_RACEMENU_SWF "${MPS_SOURCE_DIR}/interface/racesex_menu.swf")

foreach(required_path IN ITEMS "${MPS_RACEMENU_SOURCE}" "${MPS_RACEMENU_SWF}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "RaceMenu weight-sync input is missing: ${required_path}")
    endif()
endforeach()

file(READ "${MPS_RACEMENU_SOURCE}" racemenu_source)
string(REGEX REPLACE "[ \t\r\n]+" "" compact_source "${racemenu_source}")

set(
    required_fragments
    "elseif(_loc7_.callbackName==\"ChangeSex\"||_loc7_.callbackName==\"ChangeRace\")"
    "_loc2_.updateItemDescriptor();_loc2_.suspendPhysiqueWeightSyncForRebuild();"
    "this._sliderInitializeInterval=setInterval(this,\"InitializeSliders\",500);"
    "this.itemList.requestInvalidate();this.schedulePhysiqueWeightSyncAfterRebuild();clearInterval(this._sliderInitializeInterval);deletethis._sliderInitializeInterval;"
    "functionrequestUpdate(pendingData){this._pendingData=pendingData;if(!this._updateInterval){this._updateInterval=setInterval(this,\"processDataUpdate\",100,pendingData);"
    "functionisPhysiqueWeightSyncContextActive(){returnthis.bPhysiqueBodyCategoryActive&&this.bPhysiqueSlidersModeActive&&!this.textEntry.enabled&&!this.colorField.enabled&&!this.makeupPanel.enabled;}"
    "functionsyncWeightSliderFromPhysique(){if(!this.isPhysiqueWeightSyncContextActive()||this.bPhysiqueWeightSyncRebuilding){returnundefined;}"
    "functionstartPhysiqueWeightSync(){if(!this.isPhysiqueWeightSyncContextActive()||this.bPhysiqueWeightSyncRebuilding){returnundefined;}this.stopPhysiqueWeightSync();this.cancelPhysiqueWeightSyncResume();"
    "functionsuspendPhysiqueWeightSyncForRebuild(){this.bPhysiqueWeightSyncRebuilding=true;this.stopPhysiqueWeightSync();this.cancelPhysiqueWeightSyncResume();}"
    "functionschedulePhysiqueWeightSyncAfterRebuild(){this.stopPhysiqueWeightSync();this.cancelPhysiqueWeightSyncResume();"
    "functionresumePhysiqueWeightSyncAfterRebuild(){this.cancelPhysiqueWeightSyncResume();this.bPhysiqueWeightSyncRebuilding=false;if(this.isPhysiqueWeightSyncContextActive()){"
    "this.physiqueWeightSyncResumeInterval=setInterval(this,\"resumePhysiqueWeightSyncAfterRebuild\",100);"
    "this.physiqueWeightSyncInterval=setInterval(this,\"syncWeightSliderFromPhysique\",50);"
    "functionstopPhysiqueWeightSync(){if(this.physiqueWeightSyncInterval!=null){clearInterval(this.physiqueWeightSyncInterval);deletethis.physiqueWeightSyncInterval;}"
    "functioncancelPhysiqueWeightSyncResume(){if(this.physiqueWeightSyncResumeInterval!=null){clearInterval(this.physiqueWeightSyncResumeInterval);deletethis.physiqueWeightSyncResumeInterval;}"
    "functiononChangeMode(event){this.bPhysiqueSlidersModeActive=event.index==0;if(this.bPhysiqueSlidersModeActive){this.startPhysiqueWeightSync();}else{this.stopPhysiqueWeightSync();}"
    "if(this.isPhysiqueWeightSyncContextActive()&&this.categoryList.selectedEntry!=undefined)"
    "this.syncPhysiqueOverlayState();if(abShowTextEntry){this.stopPhysiqueWeightSync();}else{this.startPhysiqueWeightSync();}"
    "this.syncPhysiqueOverlayState();if(bShowField){this.stopPhysiqueWeightSync();}else{this.startPhysiqueWeightSync();}"
    "this.syncPhysiqueOverlayState();if(bShowPanel){this.stopPhysiqueWeightSync();}else{this.startPhysiqueWeightSync();}"
    "clearInterval(this._sliderInitializeInterval);deletethis._sliderInitializeInterval;}this.stopPhysiqueWeightSync();this.cancelPhysiqueWeightSyncResume();this.bPhysiqueWeightSyncRebuilding=false;"
)

foreach(required_fragment IN LISTS required_fragments)
    string(FIND "${compact_source}" "${required_fragment}" fragment_offset)
    if(fragment_offset EQUAL -1)
        message(
            FATAL_ERROR
            "RaceMenu rebuild-safe Weight synchronization fragment is missing: ${required_fragment}"
        )
    endif()
endforeach()

# SetSliders contains explanatory comments between the function header and the
# suspension call. Check the function boundary and ordering explicitly so the
# guard remains stable when comments change, while still requiring the old list
# polling to stop before any slider entries are removed.
string(FIND "${compact_source}" "functionSetSliders()" set_sliders_offset)
string(FIND "${compact_source}" "functionInitializeSliders()" initialize_sliders_offset)
if(set_sliders_offset EQUAL -1 OR initialize_sliders_offset EQUAL -1)
    message(FATAL_ERROR "RaceMenu SetSliders/InitializeSliders boundary is missing")
endif()
math(EXPR set_sliders_length "${initialize_sliders_offset} - ${set_sliders_offset}")
string(
    SUBSTRING
    "${compact_source}"
    ${set_sliders_offset}
    ${set_sliders_length}
    set_sliders_source
)
string(
    FIND
    "${set_sliders_source}"
    "this.suspendPhysiqueWeightSyncForRebuild();"
    set_sliders_suspend_offset
)
string(
    FIND
    "${set_sliders_source}"
    "if(this.itemList.entryList.length>0)"
    set_sliders_mutation_offset
)
if(
    set_sliders_suspend_offset EQUAL -1
    OR set_sliders_mutation_offset EQUAL -1
    OR set_sliders_suspend_offset GREATER_EQUAL set_sliders_mutation_offset
)
    message(
        FATAL_ERROR
        "RaceMenu SetSliders must suspend Weight synchronization before mutating the list"
    )
endif()
string(FIND "${set_sliders_source}" "this._updateInterval" set_sliders_shared_timer_offset)
if(set_sliders_shared_timer_offset GREATER -1)
    message(
        FATAL_ERROR
        "RaceMenu SetSliders must not share the pending-data update timer"
    )
endif()

string(FIND "${compact_source}" "functionSetMakeup(" set_makeup_offset)
if(set_makeup_offset EQUAL -1 OR set_makeup_offset LESS_EQUAL initialize_sliders_offset)
    message(FATAL_ERROR "RaceMenu InitializeSliders/SetMakeup boundary is missing")
endif()
math(EXPR initialize_sliders_length "${set_makeup_offset} - ${initialize_sliders_offset}")
string(
    SUBSTRING
    "${compact_source}"
    ${initialize_sliders_offset}
    ${initialize_sliders_length}
    initialize_sliders_source
)
string(
    FIND
    "${initialize_sliders_source}"
    "this._updateInterval"
    initialize_sliders_shared_timer_offset
)
if(initialize_sliders_shared_timer_offset GREATER -1)
    message(
        FATAL_ERROR
        "RaceMenu InitializeSliders must not clear the pending-data update timer"
    )
endif()

# The active 50 ms poll and the delayed 100 ms resume are deliberately separate.
# If stopPhysiqueWeightSync cancels the delayed resume, leaving or re-entering Body
# during a rebuild can strand bPhysiqueWeightSyncRebuilding in the true state.
string(FIND "${compact_source}" "functionstopPhysiqueWeightSync()" stop_sync_offset)
string(FIND "${compact_source}" "functioncancelPhysiqueWeightSyncResume()" cancel_resume_offset)
if(stop_sync_offset EQUAL -1 OR cancel_resume_offset EQUAL -1)
    message(FATAL_ERROR "RaceMenu Weight-sync timer function boundary is missing")
endif()
math(EXPR stop_sync_length "${cancel_resume_offset} - ${stop_sync_offset}")
string(
    SUBSTRING
    "${compact_source}"
    ${stop_sync_offset}
    ${stop_sync_length}
    stop_sync_source
)
string(FIND "${stop_sync_source}" "physiqueWeightSyncResumeInterval" stop_resume_timer_offset)
string(FIND "${stop_sync_source}" "cancelPhysiqueWeightSyncResume(" stop_cancel_resume_offset)
if(stop_resume_timer_offset GREATER -1 OR stop_cancel_resume_offset GREATER -1)
    message(
        FATAL_ERROR
        "stopPhysiqueWeightSync must not cancel the delayed rebuild resume"
    )
endif()

# InitializeSliders must always hold the rebuild gate for the full settling
# window. Body/tab state is evaluated only when the one-shot fires, otherwise a
# quick leave/re-enter can restart the poll before list invalidation settles.
string(
    FIND
    "${compact_source}"
    "functionschedulePhysiqueWeightSyncAfterRebuild()"
    schedule_sync_offset
)
string(
    FIND
    "${compact_source}"
    "functionresumePhysiqueWeightSyncAfterRebuild()"
    resume_sync_offset
)
if(schedule_sync_offset EQUAL -1 OR resume_sync_offset LESS_EQUAL schedule_sync_offset)
    message(FATAL_ERROR "RaceMenu rebuild schedule/resume boundary is missing")
endif()
math(EXPR schedule_sync_length "${resume_sync_offset} - ${schedule_sync_offset}")
string(
    SUBSTRING
    "${compact_source}"
    ${schedule_sync_offset}
    ${schedule_sync_length}
    schedule_sync_source
)
string(
    FIND
    "${schedule_sync_source}"
    "this.physiqueWeightSyncResumeInterval=setInterval(this,\"resumePhysiqueWeightSyncAfterRebuild\",100);"
    schedule_timer_offset
)
if(schedule_timer_offset EQUAL -1)
    message(FATAL_ERROR "RaceMenu rebuild settling timer is missing from the schedule function")
endif()
foreach(forbidden_schedule_gate IN ITEMS "bPhysiqueBodyCategoryActive" "bPhysiqueSlidersModeActive")
    string(FIND "${schedule_sync_source}" "${forbidden_schedule_gate}" schedule_gate_offset)
    if(schedule_gate_offset GREATER -1)
        message(
            FATAL_ERROR
            "RaceMenu rebuild settling timer must be scheduled independently of ${forbidden_schedule_gate}"
        )
    endif()
endforeach()

# Both native ChangeRace entry points must stop the poll before asking RaceMenu
# to rebuild. A single occurrence leaves either mouse activation or the bottom
# bar action vulnerable to the old ticking/flicker loop.
set(
    change_race_transition_fragment
    "this.loadingIcon._visible=true;this.suspendPhysiqueWeightSyncForRebuild();gfx.io.GameDelegate.call(\"ChangeRace\""
)
function(require_change_race_transition start_marker end_marker route_name)
    string(FIND "${compact_source}" "${start_marker}" route_start_offset)
    string(FIND "${compact_source}" "${end_marker}" route_end_offset)
    if(route_start_offset EQUAL -1 OR route_end_offset LESS_EQUAL route_start_offset)
        message(FATAL_ERROR "RaceMenu ${route_name} function boundary is missing")
    endif()
    math(EXPR route_length "${route_end_offset} - ${route_start_offset}")
    string(
        SUBSTRING
        "${compact_source}"
        ${route_start_offset}
        ${route_length}
        route_source
    )
    string(
        FIND
        "${route_source}"
        "${change_race_transition_fragment}"
        route_transition_offset
    )
    if(route_transition_offset EQUAL -1)
        message(
            FATAL_ERROR
            "RaceMenu ${route_name} must suspend Weight synchronization before ChangeRace"
        )
    endif()
endfunction()

require_change_race_transition(
    "functiononItemPress(event)"
    "functiononItemPressSecondary(event)"
    "onItemPress"
)
require_change_race_transition(
    "functiononChangeRaceClicked()"
    "functiononChooseColorClicked()"
    "onChangeRaceClicked"
)

# Pin the reviewed source/artifact pair. FFDec 26.0.0 produces the SWF below
# byte-for-byte from this source and the tracked base artifact. Pinning both
# hashes catches source-only edits as well as stale or independently changed SWFs.
# Normalize line endings so Git's Windows checkout policy cannot invalidate the
# source half of the pair without changing ActionScript semantics.
string(REPLACE "\r\n" "\n" normalized_racemenu_source "${racemenu_source}")
string(REPLACE "\r" "\n" normalized_racemenu_source "${normalized_racemenu_source}")
string(SHA256 racemenu_source_sha256 "${normalized_racemenu_source}")
file(SHA256 "${MPS_RACEMENU_SWF}" racemenu_swf_sha256)
set(
    expected_racemenu_source_sha256
    "fb654a80cc18b0083443253a33c9a5cab8c898fd6f3f94410290a24cd84aa052"
)
set(
    expected_racemenu_swf_sha256
    "9136f6d50d9ec952f926acf01b4cfd2c221bccdb4342754220231956c26f514b"
)
if(NOT racemenu_source_sha256 STREQUAL expected_racemenu_source_sha256)
    message(
        FATAL_ERROR
        "RaceMenu source does not match the reviewed Weight-sync source: ${racemenu_source_sha256}"
    )
endif()
if(NOT racemenu_swf_sha256 STREQUAL expected_racemenu_swf_sha256)
    message(
        FATAL_ERROR
        "RaceMenu SWF does not match the reviewed Weight-sync artifact: ${racemenu_swf_sha256}"
    )
endif()
