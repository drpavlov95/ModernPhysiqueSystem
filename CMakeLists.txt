# It's recommended to set a minimum CMake version.
# If you use CMake features from higher versions, update this to match.
cmake_minimum_required(VERSION 3.21)

# Set your project name. This will be the name of your SKSE .dll file.
project(ModernPhysiqueSystem VERSION 0.1.0 LANGUAGES CXX)

#

# YOU DO NOT NEED TO EDIT ANYTHING BELOW HERE
#
# Please see the "Project setup" section of README.md
#

# Ordinary CMake builds are side-effect free. Post-build deployment must be
# explicitly enabled as well as given a destination; environment variables and
# a stale destination cache alone are intentionally insufficient.
option(
    MPS_ENABLE_POST_BUILD_DEPLOY
    "Allow CMake post-build commands to copy the SKSE plugin"
    OFF
)
set(
    MPS_DEPLOY_OUTPUT
    ""
    CACHE PATH
    "Deploy output root folder (contains SKSE/Plugins)"
)
if(MPS_ENABLE_POST_BUILD_DEPLOY)
    if(NOT MPS_DEPLOY_OUTPUT)
        message(
            FATAL_ERROR
            "MPS_ENABLE_POST_BUILD_DEPLOY requires an explicit MPS_DEPLOY_OUTPUT"
        )
    endif()
    set(OUTPUT_FOLDER "${MPS_DEPLOY_OUTPUT}")
endif()

# Setup your SKSE plugin as an SKSE plugin!
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${headers} ${sources}) # <--- specifies plugin.cpp

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) # <--- use C++23 standard
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h) # <--- PCH.h is required!

# The runtime DLL must use the Release STL ABI because RaceMenu's public face
# interface passes SKEEFixedString (which owns std::string) across the DLL
# boundary. Keep full symbols for crash analysis without deploying a Debug ABI.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
endif()

find_package(Git QUIET)
set(MPS_GIT_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MPS_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MPS_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MPS_GIT_STATUS)
        set(MPS_GIT_COMMIT "${MPS_GIT_COMMIT}-dirty")
    endif()
endif()

string(TIMESTAMP MPS_BUILD_STAMP "%Y-%m-%d %H:%M:%S")
target_compile_definitions(
    "${PROJECT_NAME}"
    PRIVATE
        MPS_GIT_COMMIT="${MPS_GIT_COMMIT}"
        MPS_BUILD_STAMP="${MPS_BUILD_STAMP}"
)

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		${SIMPLEINI_INCLUDE_DIRS}
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
)
# Optional post-build deployment. This is disabled in all ordinary presets;
# tools/BuildAndDeployDebug.ps1 performs the reviewed paired deployment instead.
if(MPS_ENABLE_POST_BUILD_DEPLOY)

    # Copy the SKSE plugin .dll files into the SKSE/Plugins/ folder
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    set(SRC_CONFIG_FOLDER "${CMAKE_SOURCE_DIR}/config")
    
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
    if (EXISTS "${SRC_CONFIG_FOLDER}")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC_CONFIG_FOLDER}/ModernPhysiqueSystem.ini" "${DLL_FOLDER}/ModernPhysiqueSystem.ini"
            VERBATIM
        )
    endif()
    # If you perform a "Debug" build, also copy .pdb file (for debug symbols)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Host-side unit tests (no CommonLibSSE, no game deps)
# ---------------------------------------------------------------------------
option(MPS_BUILD_TESTS "Build host-side unit tests" ON)
if(MPS_BUILD_TESTS)
    find_package(doctest CONFIG REQUIRED)
    set(MPS_CORE_SOURCES
        src/core/Text.cpp
        src/core/MorphMath.cpp
        src/core/NormalMapCatalog.cpp
        src/core/FaceRouteState.cpp
        src/core/FaceGeometrySafety.cpp
    )
    set(MPS_TEST_SOURCES
        tests/cpp/test_main.cpp
        tests/cpp/test_sanity.cpp
        tests/cpp/test_text.cpp
        tests/cpp/test_morphmath.cpp
        tests/cpp/test_normalmap.cpp
        tests/cpp/test_faceroutestate.cpp
        tests/cpp/test_facegeometrysafety.cpp
    )
    add_executable(mps_tests ${MPS_CORE_SOURCES} ${MPS_TEST_SOURCES})
    target_compile_features(mps_tests PRIVATE cxx_std_23)
    target_include_directories(mps_tests PRIVATE src)
    target_link_libraries(mps_tests PRIVATE doctest::doctest)
    enable_testing()
    add_test(NAME mps_tests COMMAND mps_tests)
    add_test(
        NAME mps_face_route_static_safety
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMPS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/FaceRouteStaticSafety.cmake"
    )
    add_test(
        NAME mps_normal_map_bridge_static_safety
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMPS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/NormalMapBridgeStaticSafety.cmake"
    )
    add_test(
        NAME mps_racemenu_weight_sync_static_safety
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMPS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/RaceMenuWeightSyncStaticSafety.cmake"
    )
    add_test(
        NAME mps_deploy_static_safety
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMPS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/DeploySafetyStatic.cmake"
    )
    add_test(
        NAME mps_shared_face_morph_static_safety
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMPS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/SharedFaceMorphSafety.cmake"
    )
    add_test(
        NAME mps_face_slider_asset_safety
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMPS_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/FaceSliderAssetSafety.cmake"
    )
endif()
