# The same WGSL raster program runs through WebGPU in the browser and wgpu-native
# on Linux. Keep all SDK types and dependencies private to this component.
set(DRAKAN_GPU_DEFAULT OFF)
if(EMSCRIPTEN OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"))
  set(DRAKAN_GPU_DEFAULT ON)
endif()
option(DRAKAN_PORTABLE_GPU "Build the portable GPU compute rasterizer" ${DRAKAN_GPU_DEFAULT})
if(DRAKAN_PORTABLE_GPU)
  file(READ "${CMAKE_CURRENT_LIST_DIR}/../runtime/gs/src/portable_gpu.wgsl" DRAKAN_GPU_WGSL)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../runtime/gs/src/portable_gpu.wgsl")
  configure_file("${CMAKE_CURRENT_LIST_DIR}/../runtime/gs/src/portable_gpu_shader.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/portable_gpu_shader.cpp" @ONLY)
  add_library(drakan_portable_gpu STATIC "${CMAKE_CURRENT_BINARY_DIR}/portable_gpu_shader.cpp")
  target_include_directories(drakan_portable_gpu PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../runtime/gs/src")
  target_link_libraries(drakan_portable_gpu PRIVATE drakan::options)
  if(EMSCRIPTEN)
    target_sources(drakan_portable_gpu PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runtime/gs/src/portable_gpu_web.cpp")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    include(FetchContent)
    FetchContent_Declare(drakan_wgpu
      URL "https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1/wgpu-linux-x86_64-release.zip"
      URL_HASH SHA256=95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a)
    FetchContent_GetProperties(drakan_wgpu)
    if(NOT drakan_wgpu_POPULATED)
      FetchContent_Populate(drakan_wgpu)
    endif()
    # Ship the pinned shared runtime beside the player, with an origin-relative
    # search path. No system SDK or driver-independent library installation.
    target_sources(drakan_portable_gpu PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../runtime/gs/src/portable_gpu_native.cpp")
    target_include_directories(drakan_portable_gpu PRIVATE "${drakan_wgpu_SOURCE_DIR}/include")
    add_library(drakan_wgpu_native SHARED IMPORTED GLOBAL)
    set_target_properties(drakan_wgpu_native PROPERTIES
      IMPORTED_LOCATION "${drakan_wgpu_SOURCE_DIR}/lib/libwgpu_native.so" IMPORTED_NO_SONAME TRUE)
    target_link_libraries(drakan_portable_gpu PRIVATE drakan_wgpu_native ${CMAKE_DL_LIBS} m)
  else()
    message(FATAL_ERROR "Portable GPU currently supports Linux x64 and Emscripten; set DRAKAN_PORTABLE_GPU=OFF elsewhere")
  endif()
  add_library(drakan::portable_gpu ALIAS drakan_portable_gpu)
endif()

function(drakan_attach_portable_gpu target)
  if(NOT TARGET drakan::portable_gpu)
    return()
  endif()
  set(runtime "${DRAKAN_PS2RECOMP_DIR}/ps2xRuntime")
  target_sources(${target} PRIVATE "${runtime}/src/lib/gs/gs_gpu_raster.cpp")
  # Do not propagate SDK includes/defines into the thousands of VU AOT units.
  target_link_libraries(${target} PRIVATE "$<LINK_ONLY:drakan::portable_gpu>")
  set_property(SOURCE "${runtime}/src/lib/gs/gs_cpu_backend.cpp" TARGET_DIRECTORY ${target}
    APPEND PROPERTY COMPILE_DEFINITIONS PS2X_HAVE_PORTABLE_GPU=1)
  if(EMSCRIPTEN)
    set_property(SOURCE "${runtime}/src/lib/drakan_web.cpp" TARGET_DIRECTORY ${target}
      APPEND PROPERTY COMPILE_DEFINITIONS PS2X_HAVE_PORTABLE_GPU=1)
  endif()
  set_property(SOURCE "${runtime}/src/lib/gs/gs_gpu_raster.cpp" TARGET_DIRECTORY ${target}
    APPEND PROPERTY INCLUDE_DIRECTORIES "${DRAKAN_ROOT_DIR}/runtime/gs/src")
  if(NOT MSVC)
    set_property(SOURCE "${runtime}/src/lib/gs/gs_gpu_raster.cpp" TARGET_DIRECTORY ${target}
      APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
  endif()
endfunction()

function(drakan_stage_portable_gpu target)
  if(TARGET drakan_wgpu_native)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:drakan_wgpu_native>" "$<TARGET_FILE_DIR:${target}>"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/wgpu-native"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DRAKAN_ROOT_DIR}/third_party/wgpu-native/LICENSE.APACHE"
        "${DRAKAN_ROOT_DIR}/third_party/wgpu-native/LICENSE.MIT"
        "$<TARGET_FILE_DIR:${target}>/licenses/wgpu-native"
      VERBATIM)
  endif()
endfunction()
