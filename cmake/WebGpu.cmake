# The one webgpu.h implementation per target (docs/proposals/webgpu-renderer.md):
#   Windows x64 / Linux x64   wgpu-native SDK (Vulkan, with D3D12 / GL fallbacks), shipped beside the player
#   Emscripten                the browser's WebGPU through Emscripten's emdawnwebgpu port
# Consumers link drakan_webgpu_sdk privately; drakan_stage_webgpu(<target>) copies the native runtime next to it.
include_guard(GLOBAL)

set(DRAKAN_WEBGPU_AVAILABLE OFF)
add_library(drakan_webgpu_sdk INTERFACE)

set(_drakan_webgpu_default OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_drakan_webgpu_default ON)
endif()
option(DRAKAN_NATIVE_WEBGPU "Use native WebGPU. Windows also supports a D3D11-only build" ${_drakan_webgpu_default})
set(DRAKAN_WGPU_ROOT "$ENV{DRAKAN_WGPU_ROOT}" CACHE PATH "Extracted wgpu-native SDK with include/ and lib/")

if(EMSCRIPTEN)
  target_compile_options(drakan_webgpu_sdk INTERFACE "--use-port=emdawnwebgpu")
  target_link_options(drakan_webgpu_sdk INTERFACE "--use-port=emdawnwebgpu")
  set(DRAKAN_WEBGPU_AVAILABLE ON)
elseif((DRAKAN_NATIVE_WEBGPU OR DRAKAN_WGPU_ROOT) AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
       (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  if(WIN32)
    set(_drakan_wgpu_archive "wgpu-windows-x86_64-msvc-release.zip")
    set(_drakan_wgpu_library "wgpu_native.dll")
  else()
    set(_drakan_wgpu_archive "wgpu-linux-x86_64-release.zip")
    set(_drakan_wgpu_library "libwgpu_native.so")
  endif()
  if(DRAKAN_WGPU_ROOT)
    set(drakan_wgpu_SOURCE_DIR "${DRAKAN_WGPU_ROOT}")
  else()
    include(FetchContent)
    FetchContent_Declare(drakan_wgpu
      URL "https://github.com/gfx-rs/wgpu-native/releases/latest/download/${_drakan_wgpu_archive}")
    FetchContent_MakeAvailable(drakan_wgpu)
  endif()
  if(NOT EXISTS "${drakan_wgpu_SOURCE_DIR}/include/webgpu/webgpu.h" OR
     NOT EXISTS "${drakan_wgpu_SOURCE_DIR}/include/webgpu/wgpu.h" OR
     NOT EXISTS "${drakan_wgpu_SOURCE_DIR}/lib/${_drakan_wgpu_library}" OR
     (WIN32 AND NOT EXISTS "${drakan_wgpu_SOURCE_DIR}/lib/wgpu_native.dll.lib"))
    message(FATAL_ERROR "Native WebGPU needs an extracted wgpu-native SDK. Set DRAKAN_WGPU_ROOT to a complete SDK.")
  endif()
  add_library(drakan_wgpu_native SHARED IMPORTED GLOBAL)
  set_target_properties(drakan_wgpu_native PROPERTIES
    IMPORTED_LOCATION "${drakan_wgpu_SOURCE_DIR}/lib/${_drakan_wgpu_library}")
  if(WIN32)
    set_target_properties(drakan_wgpu_native PROPERTIES
      IMPORTED_IMPLIB "${drakan_wgpu_SOURCE_DIR}/lib/wgpu_native.dll.lib")
  else()
    set_target_properties(drakan_wgpu_native PROPERTIES IMPORTED_NO_SONAME TRUE)
    target_link_libraries(drakan_webgpu_sdk INTERFACE ${CMAKE_DL_LIBS} m)
  endif()
  target_include_directories(drakan_webgpu_sdk INTERFACE "${drakan_wgpu_SOURCE_DIR}/include")
  target_link_libraries(drakan_webgpu_sdk INTERFACE drakan_wgpu_native)
  target_compile_definitions(drakan_webgpu_sdk INTERFACE DRAKAN_WGPU_NATIVE=1)
  set(DRAKAN_WEBGPU_AVAILABLE ON)
  if(WIN32 AND NOT DRAKAN_DXC_RUNTIME_DIR)
    # D3D12 compiles WGSL with DXC when dxcompiler.dll and dxil.dll sit beside the executable (gpu/src/Context.cpp);
    # wgpu's FXC fallback hangs on the ray tracing shader. Newest Windows SDK first.
    file(GLOB _drakan_dxc_dirs "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
    list(SORT _drakan_dxc_dirs COMPARE NATURAL ORDER DESCENDING)
    foreach(_dir IN LISTS _drakan_dxc_dirs)
      if(EXISTS "${_dir}/dxcompiler.dll" AND EXISTS "${_dir}/dxil.dll")
        set(DRAKAN_DXC_RUNTIME_DIR "${_dir}" CACHE PATH "Folder with dxcompiler.dll and dxil.dll, staged beside WebGPU executables")
        break()
      endif()
    endforeach()
    if(NOT DRAKAN_DXC_RUNTIME_DIR)
      message(STATUS "WebGPU: no Windows SDK dxcompiler.dll / dxil.dll; D3D12 uses FXC and WebGPU ray tracing is off there")
    endif()
  endif()
endif()

# Ship the native runtime and its licences beside an executable (origin-relative search path on Linux), and on
# Windows the Windows SDK's DXC.
function(drakan_stage_webgpu target)
  if(NOT TARGET drakan_wgpu_native)
    return()
  endif()
  if(NOT WIN32)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
  endif()
  set(dxc_dir "")
  if(WIN32 AND DRAKAN_DXC_RUNTIME_DIR)
    set(dxc_dir "${DRAKAN_DXC_RUNTIME_DIR}")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-Dstage_dir=$<TARGET_FILE_DIR:${target}>"
      "-Dstage_lock=${CMAKE_BINARY_DIR}/CMakeFiles/drakan-wgpu-stage.lock"
      "-Druntime_file=$<TARGET_FILE:drakan_wgpu_native>"
      "-Dlicense_dir=${DRAKAN_ROOT_DIR}/third_party/wgpu-native"
      "-Ddxc_dir=${dxc_dir}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageWebGpu.cmake"
    VERBATIM)
endfunction()
