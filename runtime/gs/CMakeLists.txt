# gsr: the GS renderer (drakan::gs). The GS emulation (gsr_core) drives one of two backends, chosen at run time
# (gsr::Config::api, drakan.ini [video] renderer): the shared WebGPU backend (every platform, the default) or Direct3D 11
# (Windows only; DLSS and DXR ray tracing). Library variants: gsr (D3D11 only), gsr_webgpu (WebGPU only), gsr_both.
#   drakan::gs_headers   public headers only (gsr/*.h incl. the object-motion job metadata); every platform
#   drakan::gs           the renderer: gsr_both on Windows (WebGPU and D3D11), gsr_webgpu on Linux and in the browser
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_gs LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(gs)

# Object-motion job metadata is standard C++, also used by generated/runtime headers and non-Windows executors.
add_library(drakan_gs_headers INTERFACE)
add_library(drakan::gs_headers ALIAS drakan_gs_headers)
target_include_directories(drakan_gs_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Texture hashes, PNG/DDS pack decoding and upscaling are shared across renderers.
# Keep them available even when the Direct3D renderer cannot be built.
find_package(Threads REQUIRED)
add_library(drakan_texture STATIC src/gs_texup.cpp src/gs_texup.h src/texture_pack.cpp include/gsr/texture_pack.h)
add_library(drakan::texture ALIAS drakan_texture)
target_link_libraries(drakan_texture PUBLIC drakan::gs_headers PRIVATE drakan::options Threads::Threads)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PortableGpu.cmake")

if(EMSCRIPTEN)
  target_link_options(drakan_texture INTERFACE "-sFETCH=1")
endif()

if(DRAKAN_BUILD_RUNTIME_TESTS)
  if(TARGET drakan::portable_gpu)
    add_executable(gsr_test_portable_gpu tests/test_portable_gpu.cpp)
    target_link_libraries(gsr_test_portable_gpu PRIVATE drakan::portable_gpu)
    if(EMSCRIPTEN)
      target_compile_options(drakan_portable_gpu PRIVATE -pthread -fwasm-exceptions)
      target_compile_options(gsr_test_portable_gpu PRIVATE -pthread -fwasm-exceptions)
      target_link_options(gsr_test_portable_gpu PRIVATE -pthread -fwasm-exceptions
        "-sPROXY_TO_PTHREAD=1" "-sPTHREAD_POOL_SIZE=1" "-sEXIT_RUNTIME=1" "-sALLOW_BLOCKING_ON_MAIN_THREAD=0")
      add_custom_command(TARGET gsr_test_portable_gpu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/tests/portable_gpu.html"
          "$<TARGET_FILE_DIR:gsr_test_portable_gpu>/portable_gpu.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DRAKAN_ROOT_DIR}/runtime/web/portable_gpu_worker.js"
          "$<TARGET_FILE_DIR:gsr_test_portable_gpu>/portable_gpu_worker.js" VERBATIM)
    else()
      add_test(NAME portable_gpu COMMAND gsr_test_portable_gpu)
      set_tests_properties(portable_gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
  endif()
  add_executable(gsr_test_texup tests/test_texup.cpp)
  target_link_libraries(gsr_test_texup PRIVATE drakan::texture drakan::options)
  target_include_directories(gsr_test_texup PRIVATE src)
  add_test(NAME texup COMMAND gsr_test_texup)
endif()

# The renderer's GPU interface on WebGPU (rhi.h): every target with a WebGPU implementation.
if(TARGET drakan::gpu)
  add_library(drakan_gs_rhi STATIC src/rhi.h src/rhi_open.h src/rhi_webgpu.cpp src/rhi_proxy.cpp)
  add_library(drakan::gs_rhi ALIAS drakan_gs_rhi)
  target_include_directories(drakan_gs_rhi PUBLIC src)
  target_link_libraries(drakan_gs_rhi PUBLIC drakan::gpu PRIVATE drakan::options)
  if(DRAKAN_BUILD_RUNTIME_TESTS AND NOT EMSCRIPTEN)
    add_executable(gsr_test_rhi tests/test_rhi.cpp)
    target_link_libraries(gsr_test_rhi PRIVATE drakan_gs_rhi drakan::options)
    drakan_stage_webgpu(gsr_test_rhi)
    add_test(NAME rhi COMMAND gsr_test_rhi)
    set_tests_properties(rhi PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  endif()
endif()

# The GS emulation shared by both backends.
add_library(gsr_core STATIC
  src/gs_core.cpp
  src/image_io.cpp
  src/runtime_flags.cpp src/runtime_flags.h
  src/motion_history.cpp src/motion_history.h
  src/guide_camera.cpp src/guide_camera.h
  src/ray_scene.cpp src/ray_scene.h src/ray_bvh.cpp src/ray_bvh.h src/ray_normals.h src/surface_geometry.h src/surface_builder.h src/resident_scene.h
  src/gs_internal.h src/backend_select.inl
  include/gsr/gsr.h include/gsr/movie_frames.h)
target_include_directories(gsr_core PUBLIC src)
target_link_libraries(gsr_core PUBLIC drakan::gs_headers drakan::texture PRIVATE drakan::options)
if(WIN32)
  target_compile_definitions(gsr_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# The WebGPU backend (docs/proposals/webgpu-renderer.md): every target with a WebGPU implementation.
if(TARGET drakan::gs_rhi)
  add_library(gsr_webgpu_backend STATIC
    src/wgpu_backend.cpp src/wgpu_backend_impl.h src/wgpu_device.cpp src/wgpu_targets.cpp src/wgpu_textures.cpp
    src/wgpu_draw.cpp src/wgpu_present.cpp src/wgpu_capture.cpp src/wgpu_backdrop.cpp src/wgpu_movie.cpp
    src/wgpu_temporal.cpp src/wgpu_ray.cpp src/wgpu_ray.h src/ray_trace.wgsl src/gs_shaders_wgsl.h)
  # ray_trace.wgsl as a byte array (MSVC limits string literals to 16 KB); reconfigured when the shader changes.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ray_trace.wgsl")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/ray_trace.wgsl" GSR_RAY_WGSL HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," GSR_RAY_WGSL "${GSR_RAY_WGSL}")
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ray_trace_wgsl.cpp"
    CONTENT "namespace gsr {\nextern const char kRayTraceWgsl[];\nconst char kRayTraceWgsl[] = {${GSR_RAY_WGSL}0};\n}\n")
  target_sources(gsr_webgpu_backend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ray_trace_wgsl.cpp")
  target_include_directories(gsr_webgpu_backend PRIVATE src)
  target_link_libraries(gsr_webgpu_backend PUBLIC gsr_core PRIVATE drakan::gs_rhi drakan::texture drakan::options)
  if(WIN32)
    target_compile_definitions(gsr_webgpu_backend PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
  add_library(gsr_webgpu STATIC src/backend_select_webgpu.cpp)
  target_link_libraries(gsr_webgpu PUBLIC gsr_core gsr_webgpu_backend PRIVATE drakan::options)
  if(DRAKAN_BUILD_RUNTIME_TESTS AND NOT EMSCRIPTEN)
    # The synthetic GIF scenes and their pixel checks, rendered by the WebGPU backend (gsr_test_synthetic runs them
    # on Direct3D 11).
    set(GSR_WEBGPU_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gs-webgpu")
    add_executable(gsr_test_synthetic_webgpu tests/test_synthetic.cpp)
    target_link_libraries(gsr_test_synthetic_webgpu PRIVATE gsr_webgpu)
    target_compile_definitions(gsr_test_synthetic_webgpu PRIVATE "GSR_OUT_DIR=\"${GSR_WEBGPU_OUT_DIR}\"")
    target_include_directories(gsr_test_synthetic_webgpu PRIVATE src)
    drakan_stage_webgpu(gsr_test_synthetic_webgpu)
    add_executable(gsr_test_ray_trace tests/test_ray_trace.cpp)
    target_link_libraries(gsr_test_ray_trace PRIVATE gsr_webgpu drakan::gs_rhi)
    target_include_directories(gsr_test_ray_trace PRIVATE src)
    drakan_stage_webgpu(gsr_test_ray_trace)
    add_test(NAME ray_trace COMMAND gsr_test_ray_trace)
    set_tests_properties(ray_trace PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME synthetic_webgpu COMMAND gsr_test_synthetic_webgpu)
    set_tests_properties(synthetic_webgpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
  endif()
  # The players stage wgpu_native beside the executable.
  set(GSR_WEBGPU_SELECTED ON CACHE INTERNAL "drakan::gs includes the WebGPU backend")
  if(NOT WIN32)
    add_library(drakan::gs ALIAS gsr_webgpu) # Linux and the browser: the only GPU renderer there
  endif()
else()
  set(GSR_WEBGPU_SELECTED OFF CACHE INTERNAL "drakan::gs includes the WebGPU backend")
endif()

if(NOT WIN32)
  return()
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

set(GSR_PS2RECOMP_GS_INCLUDE "${DRAKAN_PS2RECOMP_DIR}/ps2xRuntime/include"
    CACHE PATH "PS2Recomp ps2xRuntime include dir (used only to cross-check swizzle tables in tests)")
set(GSR_BUILD_TESTS ${DRAKAN_BUILD_RUNTIME_TESTS})

add_library(gsr_d3d11_backend STATIC
  src/d3d11_backend.cpp src/d3d11_backend_impl.h
  src/d3d11_device.cpp src/d3d11_textures.cpp src/d3d11_targets.cpp src/d3d11_world.cpp src/d3d11_backdrop.cpp
  src/d3d11_draw.cpp src/d3d11_present.cpp src/d3d11_capture.cpp src/d3d11_movie.cpp
  src/temporal.cpp src/temporal.h
  src/nr_guides.cpp src/nr_guides.h src/sr_combined.cpp src/sr_combined.h
  src/ray_water.cpp src/ray_water.h
  src/gs_mem.h src/gs_texdecode.h src/gs_shaders.h)
target_include_directories(gsr_d3d11_backend PRIVATE src)
target_compile_definitions(gsr_d3d11_backend PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gsr_d3d11_backend PUBLIC gsr_core d3d11 dxgi d3dcompiler PRIVATE drakan::texture drakan::options)

# D3D11 only (its tests), and the Windows game's renderer with both backends.
add_library(gsr STATIC src/backend_select_d3d11.cpp)
target_link_libraries(gsr PUBLIC gsr_core gsr_d3d11_backend PRIVATE drakan::options)
if(TARGET gsr_webgpu_backend)
  add_library(gsr_both STATIC src/backend_select_both.cpp)
  target_link_libraries(gsr_both PUBLIC gsr_core gsr_d3d11_backend gsr_webgpu_backend PRIVATE drakan::options)
  add_library(drakan::gs ALIAS gsr_both)
else()
  add_library(drakan::gs ALIAS gsr)
endif()

# Hardware ray tracing (DXR water reflections, ray-traced shadows). Needs the D3D11/D3D12 interop helper
# (src/nr_interop.cpp) and the DirectX Shader Compiler (dxc) from the Windows SDK.
option(GSR_DXR "Build the DXR ray-traced water / shadows when dxc is available" ON)
if(GSR_DXR)
  file(GLOB GSR_DXC_SDK_DIRS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
  # Newest SDK first: old SDKs ship a dxc without shader model 6.5.
  list(SORT GSR_DXC_SDK_DIRS COMPARE NATURAL ORDER DESCENDING)
  find_program(GSR_DXC NAMES dxc HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64" ${GSR_DXC_SDK_DIRS})
  if(GSR_DXC)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ray_water_bytecode.h"
      COMMAND "${GSR_DXC}" -T cs_6_5 -E main -O3 -Ges
        -Fh "${CMAKE_CURRENT_BINARY_DIR}/ray_water_bytecode.h" -Vn g_rayWater
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ray_water.hlsl"
      DEPENDS src/ray_water.hlsl VERBATIM)
    target_sources(gsr_d3d11_backend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ray_water_bytecode.h")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ray_sky_bytecode.h"
      COMMAND "${GSR_DXC}" -T cs_6_5 -E skyMain -O3 -Ges
        -Fh "${CMAKE_CURRENT_BINARY_DIR}/ray_sky_bytecode.h" -Vn g_raySky
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ray_water.hlsl"
      DEPENDS src/ray_water.hlsl VERBATIM)
    target_sources(gsr_d3d11_backend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ray_sky_bytecode.h")
    target_include_directories(gsr_d3d11_backend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_sources(gsr_d3d11_backend PRIVATE src/nr_interop.cpp)
    target_compile_definitions(gsr_d3d11_backend PRIVATE GSR_HAVE_DXR_WATER=1)
    target_link_libraries(gsr_d3d11_backend PRIVATE d3d12)
    message(STATUS "gsr: DXR ray tracing ON (dxc: ${GSR_DXC})")
  else()
    message(STATUS "gsr: DXR ray tracing OFF (dxc not found; install the Windows 10/11 SDK)")
  endif()
endif()

# Optional DLSS Super Resolution module (NVIDIA NGX SDK, Windows / NVIDIA only; see docs/reference/dlss.md). Everything outside src/dlss_*.cpp and src/temporal.*
# only sees the gsr::Upscaler interface, so with GSR_DLSS=OFF (or no SDK) the stub is linked and the renderer is unchanged.
set(GSR_DLSS_SDK_DIR "" CACHE PATH "NVIDIA DLSS SDK checkout (github.com/NVIDIA/DLSS); user-supplied, never vendored")
option(GSR_DLSS "Build the DLSS module when the SDK is present (off by default; the drakan-dlss build turns it on)" OFF)
if(GSR_DLSS AND EXISTS "${GSR_DLSS_SDK_DIR}/include/nvsdk_ngx.h" AND EXISTS "${GSR_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
  target_sources(gsr_d3d11_backend PRIVATE src/dlss_ngx.cpp)
  target_include_directories(gsr_d3d11_backend PRIVATE "${GSR_DLSS_SDK_DIR}/include")
  target_compile_definitions(gsr_d3d11_backend PRIVATE GSR_HAVE_DLSS=1) # nothing outside the backend reads it: PUBLIC rebuilt all of ps2_runtime on every toggle
  target_link_libraries(gsr_d3d11_backend PUBLIC
    optimized "${GSR_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib"
    debug "${GSR_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d_dbg.lib")
  message(STATUS "gsr: DLSS module ON (${GSR_DLSS_SDK_DIR})")
else()
  target_sources(gsr_d3d11_backend PRIVATE src/dlss_stub.cpp)
  message(STATUS "gsr: DLSS module OFF")
endif()
if(MSVC)
  target_compile_options(gsr_d3d11_backend PRIVATE /MP)
endif()

get_filename_component(GSR_OUT_DIR_DEFAULT "${DRAKAN_ROOT_DIR}/build/gs" ABSOLUTE)
set(GSR_OUT_DIR "${GSR_OUT_DIR_DEFAULT}" CACHE PATH "Default directory for test screenshots")
file(TO_NATIVE_PATH "${GSR_OUT_DIR}" GSR_OUT_DIR_NATIVE)
string(REPLACE "\\" "\\\\" GSR_OUT_DIR_ESC "${GSR_OUT_DIR_NATIVE}")

if(GSR_BUILD_TESTS)
  add_executable(gsr_test_model_motion tests/test_model_motion.cpp)
  target_include_directories(gsr_test_model_motion PRIVATE src include)
  target_compile_options(gsr_test_model_motion PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
  add_executable(gsr_test_image_upload tests/test_image_upload.cpp)
  target_include_directories(gsr_test_image_upload PRIVATE src)
  if(MSVC)
    target_compile_options(gsr_test_image_upload PRIVATE /UNDEBUG)
  endif()
  add_executable(gsr_test_rt_readback tests/test_rt_readback.cpp)
  target_include_directories(gsr_test_rt_readback PRIVATE src)
  target_link_libraries(gsr_test_rt_readback PRIVATE d3d11 d3dcompiler)
  if(MSVC)
    target_compile_options(gsr_test_rt_readback PRIVATE /UNDEBUG)
  endif()
  add_executable(gsr_test_ray_water tests/test_ray_water.cpp)
  add_executable(gsr_test_scene_geometry tests/test_scene_geometry.cpp)
  target_include_directories(gsr_test_scene_geometry PRIVATE src "${GSR_PS2RECOMP_GS_INCLUDE}/../src/lib")
  target_link_libraries(gsr_test_scene_geometry PRIVATE gsr)
  target_compile_options(gsr_test_scene_geometry PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
  add_test(NAME scene_geometry COMMAND gsr_test_scene_geometry)
  add_executable(gsr_test_surface_builder tests/test_surface_builder.cpp)
  target_include_directories(gsr_test_surface_builder PRIVATE src include)
  target_link_libraries(gsr_test_surface_builder PRIVATE gsr)
  target_compile_options(gsr_test_surface_builder PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>)
  add_test(NAME surface_builder COMMAND gsr_test_surface_builder)
  target_include_directories(gsr_test_ray_water PRIVATE src)
  target_link_libraries(gsr_test_ray_water PRIVATE gsr d3d12)
  if(MSVC)
    target_compile_options(gsr_test_ray_water PRIVATE /UNDEBUG)
  endif()
  add_executable(gsr_test_temporal_presentation tests/test_temporal_presentation.cpp)
  target_include_directories(gsr_test_temporal_presentation PRIVATE src)
  if(MSVC)
    target_compile_options(gsr_test_temporal_presentation PRIVATE /UNDEBUG)
  endif()
  add_executable(gsr_test_rt_upload tests/test_rt_upload.cpp)
  target_include_directories(gsr_test_rt_upload PRIVATE src)
  add_executable(gsr_test_sr_sharpness tests/test_sr_sharpness.cpp src/sr_combined.cpp)
  target_include_directories(gsr_test_sr_sharpness PRIVATE src)
  target_compile_definitions(gsr_test_sr_sharpness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(gsr_test_sr_sharpness PRIVATE d3d11 d3dcompiler)
  add_executable(gsr_test_alpha_pipeline tests/test_alpha_pipeline.cpp)
  target_include_directories(gsr_test_alpha_pipeline PRIVATE src include)
  target_link_libraries(gsr_test_alpha_pipeline PRIVATE d3dcompiler)
  add_executable(gsr_test_hud_page tests/test_hud_page.cpp)
  add_executable(gsr_test_texture_decode tests/test_texture_decode.cpp)
  target_include_directories(gsr_test_texture_decode PRIVATE src include)
  add_executable(gsr_test_palette_hd tests/test_palette_hd.cpp)
  target_include_directories(gsr_test_palette_hd PRIVATE src include)
  add_executable(gsr_test_rt_sampling tests/test_rt_sampling.cpp)
  target_include_directories(gsr_test_rt_sampling PRIVATE src include)
  target_link_libraries(gsr_test_rt_sampling PRIVATE d3dcompiler)
  add_executable(gsr_test_rt_sampling_pixels tests/test_rt_sampling_pixels.cpp)
  target_include_directories(gsr_test_rt_sampling_pixels PRIVATE src)
  target_link_libraries(gsr_test_rt_sampling_pixels PRIVATE d3d11 d3dcompiler dxguid)
  if(MSVC)
    target_compile_options(gsr_test_rt_sampling_pixels PRIVATE /W4 /WX /O2)
  endif()
  add_executable(gsr_test_sprite_footprint tests/test_sprite_footprint.cpp)
  target_include_directories(gsr_test_sprite_footprint PRIVATE src)
  # Assertions are the test oracle even in a Release test build.
  if(MSVC)
    target_compile_options(gsr_test_sprite_footprint PRIVATE /UNDEBUG)
  else()
    target_compile_options(gsr_test_sprite_footprint PRIVATE -UNDEBUG)
  endif()
  add_executable(gsr_test_motion_history tests/test_motion_history.cpp src/motion_history.cpp)
  target_include_directories(gsr_test_motion_history PRIVATE src)
  add_executable(gsr_test_motion_shader tests/test_motion_shader.cpp)
  target_include_directories(gsr_test_motion_shader PRIVATE src)
  target_link_libraries(gsr_test_motion_shader PRIVATE d3d11 d3dcompiler)
  add_executable(gsr_test_stretch_depth tests/test_stretch_depth.cpp)
  target_include_directories(gsr_test_stretch_depth PRIVATE src)
  target_link_libraries(gsr_test_stretch_depth PRIVATE d3d11 d3dcompiler)
  add_executable(gsr_test_nr_guides tests/test_nr_guides.cpp src/nr_guides.cpp src/guide_camera.cpp)
  target_include_directories(gsr_test_nr_guides PRIVATE src)
  target_link_libraries(gsr_test_nr_guides PRIVATE d3d11 d3dcompiler)
  target_compile_definitions(gsr_test_nr_guides PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  add_executable(gsr_test_swizzle tests/test_swizzle.cpp)
  target_include_directories(gsr_test_swizzle PRIVATE src)
  if(EXISTS "${GSR_PS2RECOMP_GS_INCLUDE}/runtime/gs/ps2_gs_psmt8.h")
    target_include_directories(gsr_test_swizzle PRIVATE "${GSR_PS2RECOMP_GS_INCLUDE}")
    target_compile_definitions(gsr_test_swizzle PRIVATE GSR_HAVE_PS2RECOMP_TABLES=1)
  endif()

  add_executable(gsr_test_synthetic tests/test_synthetic.cpp)
  target_link_libraries(gsr_test_synthetic PRIVATE gsr)
  target_compile_definitions(gsr_test_synthetic PRIVATE "GSR_OUT_DIR=\"${GSR_OUT_DIR_ESC}\"")
  target_include_directories(gsr_test_synthetic PRIVATE src)

  add_executable(gsr_replay tests/replay.cpp)
  target_link_libraries(gsr_replay PRIVATE gsr)
  target_compile_definitions(gsr_replay PRIVATE "GSR_OUT_DIR=\"${GSR_OUT_DIR_ESC}\"")

  add_executable(texaudit tests/texaudit.cpp)
  target_link_libraries(texaudit PRIVATE gsr)
  target_include_directories(texaudit PRIVATE src)
  target_compile_definitions(texaudit PRIVATE "GSR_OUT_DIR=\"${GSR_OUT_DIR_ESC}\"")

  add_executable(gsr_test_live tests/test_live.cpp)
  target_link_libraries(gsr_test_live PRIVATE gsr psapi)
  get_filename_component(GSR_DEFAULT_CAPTURE_PATH "${DRAKAN_ROOT_DIR}/build/captures/gscap_20261016_194452.bin" ABSOLUTE)
  target_compile_definitions(gsr_test_live PRIVATE "GSR_OUT_DIR=\"${GSR_OUT_DIR_ESC}\"" "GSR_DEFAULT_CAPTURE=\"${GSR_DEFAULT_CAPTURE_PATH}\"")

  add_executable(gsr_test_scene_models tests/test_scene_models.cpp ../hle/src/IsoFs.cpp ../hle/src/IsoSourceFile.cpp)
  target_include_directories(gsr_test_scene_models PRIVATE include ../hle/include "${DRAKAN_PS2RECOMP_DIR}/ps2xRuntime/src/lib" "${DRAKAN_PS2RECOMP_DIR}/ps2xRuntime/include")
  if(MSVC)
    target_compile_options(gsr_test_scene_models PRIVATE /UNDEBUG)
  else()
    target_compile_options(gsr_test_scene_models PRIVATE -UNDEBUG)
  endif()
  add_test(NAME model_motion COMMAND gsr_test_model_motion)
  add_test(NAME image_upload COMMAND gsr_test_image_upload)
  add_test(NAME scene_models COMMAND gsr_test_scene_models)
  add_test(NAME rt_readback COMMAND gsr_test_rt_readback)
  add_test(NAME temporal_presentation COMMAND gsr_test_temporal_presentation)
  add_test(NAME ray_water COMMAND gsr_test_ray_water)
  set_tests_properties(ray_water PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_test(NAME rt_upload COMMAND gsr_test_rt_upload)
  add_test(NAME alpha_pipeline COMMAND gsr_test_alpha_pipeline)
  add_test(NAME hud_page COMMAND gsr_test_hud_page)
  add_test(NAME texture_decode COMMAND gsr_test_texture_decode)
  add_test(NAME palette_hd COMMAND gsr_test_palette_hd)
  set_tests_properties(texture_decode PROPERTIES TIMEOUT 180)
  add_test(NAME rt_sampling COMMAND gsr_test_rt_sampling)
  # Creates a software (WARP) D3D11 device; the flag keeps direct invocations from touching a device by accident.
  add_test(NAME rt_sampling_pixels COMMAND gsr_test_rt_sampling_pixels --run-warp)
  set_tests_properties(rt_sampling_pixels PROPERTIES TIMEOUT 120)
  add_test(NAME sprite_footprint COMMAND gsr_test_sprite_footprint)
  add_test(NAME sr_sharpness COMMAND gsr_test_sr_sharpness)
  add_test(NAME motion_history COMMAND gsr_test_motion_history)
  add_test(NAME motion_shader COMMAND gsr_test_motion_shader)
  add_test(NAME stretch_depth COMMAND gsr_test_stretch_depth)
  set_tests_properties(stretch_depth PROPERTIES TIMEOUT 30)
  add_test(NAME nr_guides COMMAND gsr_test_nr_guides)
  add_test(NAME swizzle COMMAND gsr_test_swizzle)
  add_test(NAME synthetic COMMAND gsr_test_synthetic)
  # Replays a local GS capture (build/captures/gscap_*.bin, never committed); exits 77 (skipped) when it is absent.
  add_test(NAME live COMMAND gsr_test_live)
  set_tests_properties(live PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
endif()

# Project warning flags for every test / tool executable defined above.
get_directory_property(GSR_DIR_TARGETS BUILDSYSTEM_TARGETS)
foreach(tgt IN LISTS GSR_DIR_TARGETS)
  get_target_property(tgt_type ${tgt} TYPE)
  if(tgt_type STREQUAL "EXECUTABLE")
    target_link_libraries(${tgt} PRIVATE drakan::options)
  endif()
endforeach()
