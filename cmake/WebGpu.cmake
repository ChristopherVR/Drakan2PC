# The one webgpu.h implementation per target (docs/proposals/webgpu-renderer.md):
#   Windows x64 / Linux x64   pinned wgpu-native release (Vulkan, with D3D12 / GL fallbacks), shipped beside the player
#   Emscripten                the browser's WebGPU through Emscripten's emdawnwebgpu port
# Consumers link drakan_webgpu_sdk privately; drakan_stage_webgpu(<target>) copies the native runtime next to it.
include_guard(GLOBAL)

set(DRAKAN_WEBGPU_AVAILABLE OFF)
set(DRAKAN_WGPU_VERSION "v29.0.1.1")
add_library(drakan_webgpu_sdk INTERFACE)

if(EMSCRIPTEN)
  target_compile_options(drakan_webgpu_sdk INTERFACE "--use-port=emdawnwebgpu")
  target_link_options(drakan_webgpu_sdk INTERFACE "--use-port=emdawnwebgpu")
  set(DRAKAN_WEBGPU_AVAILABLE ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  if(WIN32)
    set(_drakan_wgpu_archive "wgpu-windows-x86_64-msvc-release.zip")
    set(_drakan_wgpu_sha256 "7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132")
  else()
    set(_drakan_wgpu_archive "wgpu-linux-x86_64-release.zip")
    set(_drakan_wgpu_sha256 "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a")
  endif()
  include(FetchContent)
  FetchContent_Declare(drakan_wgpu
    URL "https://github.com/gfx-rs/wgpu-native/releases/download/${DRAKAN_WGPU_VERSION}/${_drakan_wgpu_archive}"
    URL_HASH SHA256=${_drakan_wgpu_sha256})
  FetchContent_GetProperties(drakan_wgpu)
  if(NOT drakan_wgpu_POPULATED)
    FetchContent_Populate(drakan_wgpu)
  endif()
  add_library(drakan_wgpu_native SHARED IMPORTED GLOBAL)
  if(WIN32)
    set_target_properties(drakan_wgpu_native PROPERTIES
      IMPORTED_LOCATION "${drakan_wgpu_SOURCE_DIR}/lib/wgpu_native.dll"
      IMPORTED_IMPLIB "${drakan_wgpu_SOURCE_DIR}/lib/wgpu_native.dll.lib")
  else()
    set_target_properties(drakan_wgpu_native PROPERTIES
      IMPORTED_LOCATION "${drakan_wgpu_SOURCE_DIR}/lib/libwgpu_native.so" IMPORTED_NO_SONAME TRUE)
    target_link_libraries(drakan_webgpu_sdk INTERFACE ${CMAKE_DL_LIBS} m)
  endif()
  target_include_directories(drakan_webgpu_sdk INTERFACE "${drakan_wgpu_SOURCE_DIR}/include")
  target_link_libraries(drakan_webgpu_sdk INTERFACE drakan_wgpu_native)
  target_compile_definitions(drakan_webgpu_sdk INTERFACE DRAKAN_WGPU_NATIVE=1)
  set(DRAKAN_WEBGPU_AVAILABLE ON)
endif()

# Ship the pinned native runtime and its licences beside an executable (origin-relative search path on Linux).
function(drakan_stage_webgpu target)
  if(NOT TARGET drakan_wgpu_native)
    return()
  endif()
  if(NOT WIN32)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:drakan_wgpu_native>" "$<TARGET_FILE_DIR:${target}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/wgpu-native"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${DRAKAN_ROOT_DIR}/third_party/wgpu-native/LICENSE.APACHE"
      "${DRAKAN_ROOT_DIR}/third_party/wgpu-native/LICENSE.MIT"
      "$<TARGET_FILE_DIR:${target}>/licenses/wgpu-native"
    VERBATIM)
endfunction()
