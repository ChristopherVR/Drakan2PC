# C++ policy tests (pacing, vblank hand-off, widescreen, scheduler, display commit). They compile in-repo ps2recomp / gs
# sources directly; no game data, raylib or FFmpeg.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_policy_checks LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(policy_checks)

set(PS2X_RUNTIME_SOURCE "${DRAKAN_PS2RECOMP_DIR}" CACHE PATH "In-repo ps2recomp source; no private assets required")
if(NOT EXISTS "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/gs_frame_activity.h")
  message(FATAL_ERROR "PS2X_RUNTIME_SOURCE must point at the in-repo ps2recomp/ directory")
endif()
set(DRAKAN_ROOT "${DRAKAN_ROOT_DIR}")
add_executable(pacing_bridge_test pacing_bridge_test.cpp)
add_executable(vblank_handoff_test vblank_handoff_test.cpp)
add_executable(frame_cadence_test frame_cadence_test.cpp)
add_executable(widescreen_policy_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/widescreen_policy_tests.cpp")
add_executable(inventory_views_policy_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/inventory_views_policy_tests.cpp")
add_executable(tab_preview_policy_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/tab_preview_policy_tests.cpp")
add_executable(pose_mirror_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/pose_mirror_tests.cpp")
foreach(target pacing_bridge_test vblank_handoff_test frame_cadence_test widescreen_policy_test
        inventory_views_policy_test tab_preview_policy_test pose_mirror_test)
  target_include_directories(${target} PRIVATE
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs"
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib"
    "${DRAKAN_ROOT}/runtime/gs/include")
  if(MSVC)
    target_compile_options(${target} PRIVATE /WX /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endforeach()

find_package(Threads REQUIRED)
add_executable(host_pad_test host_pad_test.cpp)
target_include_directories(host_pad_test PRIVATE "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
target_link_libraries(host_pad_test PRIVATE Threads::Threads)
drakan_enable_asserts(host_pad_test)
add_test(NAME host_pad COMMAND host_pad_test)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(DRAKAN_CONFIG_HOST "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/drakan_config_posix.cpp")
if(WIN32)
  set(DRAKAN_CONFIG_HOST "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/drakan_config_win.cpp")
endif()
add_executable(host_services_test host_services_test.cpp
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/drakan_config.cpp" "${DRAKAN_CONFIG_HOST}")
target_include_directories(host_services_test PRIVATE "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
drakan_enable_asserts(host_services_test)
add_test(NAME host_services COMMAND host_services_test "$<TARGET_FILE_DIR:host_services_test>"
  "${CMAKE_CURRENT_BINARY_DIR}/host services.ini")

# Exercise the production VU SIMD headers on both native compilers without game
# data or the full runtime. Covers PS2 conversion/normalization across rounding modes.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
  add_executable(vu_simd_portability_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/vu_simd_tests.cpp")
  target_include_directories(vu_simd_portability_test PRIVATE
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include" "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
  if(MSVC)
    target_compile_options(vu_simd_portability_test PRIVATE /fp:strict)
  else()
    target_compile_options(vu_simd_portability_test PRIVATE -msse4.1 -frounding-math -ffp-contract=off)
  endif()
  add_test(NAME vu_simd_portability COMMAND vu_simd_portability_test)
  add_executable(vu_rounding_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/vu_rounding_tests.cpp")
  target_include_directories(vu_rounding_test PRIVATE "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
  if(MSVC)
    target_compile_options(vu_rounding_test PRIVATE /fp:strict)
  else()
    target_compile_options(vu_rounding_test PRIVATE -msse4.1 -frounding-math -ffp-contract=off)
  endif()
  add_test(NAME vu_rounding COMMAND vu_rounding_test)
endif()
add_executable(software_frame_clock_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/software_frame_clock_tests.cpp")
target_include_directories(software_frame_clock_test PRIVATE "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
drakan_enable_asserts(software_frame_clock_test)
add_test(NAME software_frame_clock COMMAND software_frame_clock_test)

add_executable(gs_raster_math_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/gs_raster_math_tests.cpp")
target_include_directories(gs_raster_math_test PRIVATE "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
drakan_enable_asserts(gs_raster_math_test)
if(MSVC)
  target_compile_options(gs_raster_math_test PRIVATE /fp:strict)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
  target_compile_options(gs_raster_math_test PRIVATE -msse4.1 -frounding-math -ffp-contract=off)
endif()
add_test(NAME gs_raster_math COMMAND gs_raster_math_test)

add_executable(gs_cpu_tiles_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/gs_cpu_tiles_tests.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp")
target_include_directories(gs_cpu_tiles_test PRIVATE
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include" "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
target_link_libraries(gs_cpu_tiles_test PRIVATE Threads::Threads drakan::texture)
target_compile_definitions(gs_cpu_tiles_test PRIVATE PS2_RUNTIME_LOGS=0 AGRESSIVE_LOGS=0)
drakan_enable_asserts(gs_cpu_tiles_test)
if(MSVC)
  target_compile_options(gs_cpu_tiles_test PRIVATE /fp:strict)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
  target_compile_options(gs_cpu_tiles_test PRIVATE -msse4.1 -frounding-math -ffp-contract=off -fno-strict-aliasing)
endif()
add_test(NAME gs_cpu_tiles COMMAND gs_cpu_tiles_test)
set_tests_properties(gs_cpu_tiles PROPERTIES TIMEOUT 120)

add_executable(gs_hd_texture_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/gs_hd_texture_tests.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp")
target_include_directories(gs_hd_texture_test PRIVATE
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include" "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
target_link_libraries(gs_hd_texture_test PRIVATE Threads::Threads drakan::texture)
target_compile_definitions(gs_hd_texture_test PRIVATE PS2_RUNTIME_LOGS=0 AGRESSIVE_LOGS=0)
target_include_directories(gs_hd_texture_test PRIVATE "${DRAKAN_ROOT_DIR}/runtime/gs/src")
drakan_enable_asserts(gs_hd_texture_test)
if(MSVC)
  target_compile_options(gs_hd_texture_test PRIVATE /fp:strict)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
  target_compile_options(gs_hd_texture_test PRIVATE -msse4.1 -frounding-math -ffp-contract=off -fno-strict-aliasing)
endif()
add_test(NAME gs_hd_texture COMMAND gs_hd_texture_test)
set_tests_properties(gs_hd_texture PROPERTIES TIMEOUT 120)

add_executable(gs_cpu_async_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/gs_cpu_async_tests.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/gs_cpu_async_backend.cpp"
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp")
target_include_directories(gs_cpu_async_test PRIVATE
  "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include" "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib")
target_link_libraries(gs_cpu_async_test PRIVATE Threads::Threads drakan::texture)
target_compile_definitions(gs_cpu_async_test PRIVATE PS2_RUNTIME_LOGS=0 AGRESSIVE_LOGS=0)
drakan_enable_asserts(gs_cpu_async_test)
if(MSVC)
  target_compile_options(gs_cpu_async_test PRIVATE /fp:strict)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
  target_compile_options(gs_cpu_async_test PRIVATE -msse4.1 -frounding-math -ffp-contract=off -fno-strict-aliasing)
endif()
add_test(NAME gs_cpu_async COMMAND gs_cpu_async_test)
set_tests_properties(gs_cpu_async PROPERTIES TIMEOUT 120)

if(TARGET drakan::portable_gpu)
  foreach(target gs_cpu_tiles_test gs_hd_texture_test gs_cpu_async_test)
    drakan_attach_portable_gpu(${target})
  endforeach()
  target_compile_definitions(gs_hd_texture_test PRIVATE PS2X_TEST_PORTABLE_GPU=1)
  add_executable(gs_gpu_pixels_test "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/tests/gs_gpu_pixels_tests.cpp"
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp"
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp")
  target_include_directories(gs_gpu_pixels_test PRIVATE
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include" "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib"
    "${DRAKAN_ROOT_DIR}/runtime/gs/src")
  target_link_libraries(gs_gpu_pixels_test PRIVATE Threads::Threads drakan::texture)
  target_compile_definitions(gs_gpu_pixels_test PRIVATE PS2_RUNTIME_LOGS=0 AGRESSIVE_LOGS=0)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
    target_compile_options(gs_gpu_pixels_test PRIVATE -msse4.1 -ffp-contract=off -fno-strict-aliasing)
  endif()
  drakan_enable_asserts(gs_gpu_pixels_test)
  drakan_attach_portable_gpu(gs_gpu_pixels_test)
  add_test(NAME gs_gpu_pixels COMMAND gs_gpu_pixels_test)
  set_tests_properties(gs_gpu_pixels PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
endif()

set(SCHEDULER_CPP "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp")
set(SCHEDULER_HPP "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include/runtime/ee_scheduler.h")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/scheduler_policy_fixture.h"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_scheduler_policy_fixture.py"
    --source "${SCHEDULER_CPP}" --header "${SCHEDULER_HPP}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/scheduler_policy_fixture.h"
  DEPENDS generate_scheduler_policy_fixture.py "${SCHEDULER_CPP}" "${SCHEDULER_HPP}"
  VERBATIM)
add_executable(scheduler_policy_test scheduler_policy_test.cpp "${CMAKE_CURRENT_BINARY_DIR}/scheduler_policy_fixture.h")
target_include_directories(scheduler_policy_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(MSVC)
  target_compile_options(scheduler_policy_test PRIVATE /UNDEBUG)
else()
  target_compile_options(scheduler_policy_test PRIVATE -UNDEBUG)
endif()

# Real GS front end and bridge with a recording backend, no D3D/device/window.
if(WIN32)
  add_executable(native_display_commit_test native_display_commit_test.cpp
    "${DRAKAN_ROOT}/runtime/gs/src/gs_core.cpp"
    "${DRAKAN_ROOT}/runtime/gs/src/runtime_flags.cpp")
  target_include_directories(native_display_commit_test PRIVATE
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/include"
    "${PS2X_RUNTIME_SOURCE}/ps2xRuntime/src/lib/gs"
    "${DRAKAN_ROOT}/runtime/gs/include" "${DRAKAN_ROOT}/runtime/gs/src")
  target_compile_definitions(native_display_commit_test PRIVATE
    PS2X_HAVE_DRAKAN_SETTINGS)
  target_link_libraries(native_display_commit_test PRIVATE user32)
  if(MSVC)
    target_compile_options(native_display_commit_test PRIVATE /WX /UNDEBUG)
    # Existing sprite-seam f0/f1 warning in gs_core, unrelated to completion.
    set_source_files_properties("${DRAKAN_ROOT}/runtime/gs/src/gs_core.cpp"
      PROPERTIES COMPILE_OPTIONS /wd4701)
  else()
    target_compile_options(native_display_commit_test PRIVATE -UNDEBUG)
  endif()
endif()
add_test(NAME scheduler_policy COMMAND scheduler_policy_test)
add_test(NAME pacing_bridge COMMAND pacing_bridge_test)
add_test(NAME vblank_handoff COMMAND vblank_handoff_test)
add_test(NAME frame_cadence COMMAND frame_cadence_test)
add_test(NAME widescreen_policy COMMAND widescreen_policy_test)
add_test(NAME inventory_views_policy COMMAND inventory_views_policy_test)
add_test(NAME tab_preview_policy COMMAND tab_preview_policy_test)
add_test(NAME pose_mirror COMMAND pose_mirror_test)
if(TARGET native_display_commit_test)
  add_test(NAME native_display_commit COMMAND native_display_commit_test)
endif()

get_directory_property(POLICY_TARGETS BUILDSYSTEM_TARGETS)
foreach(tgt IN LISTS POLICY_TARGETS)
  get_target_property(tgt_type ${tgt} TYPE)
  if(tgt_type STREQUAL "EXECUTABLE")
    target_link_libraries(${tgt} PRIVATE drakan::options)
  endif()
endforeach()
