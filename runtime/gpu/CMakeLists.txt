# drakan::gpu - the WebGPU device layer shared by Windows, Linux and the browser (docs/proposals/webgpu-renderer.md).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_gpu LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(gpu)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/WebGpu.cmake")

if(NOT DRAKAN_WEBGPU_AVAILABLE)
    message(STATUS "drakan::gpu: no WebGPU implementation for this target")
    return()
endif()

add_library(drakan_gpu STATIC src/Context.cpp src/Surface.cpp)
add_library(drakan::gpu ALIAS drakan_gpu)
target_include_directories(drakan_gpu PUBLIC include)
# webgpu.h is part of this component's public API (the renderer uses WebGPU types directly).
target_link_libraries(drakan_gpu PUBLIC drakan_webgpu_sdk PRIVATE drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS AND NOT EMSCRIPTEN)
    add_executable(gpu_test tests/gpu_test.cpp)
    target_link_libraries(gpu_test PRIVATE drakan_gpu drakan::options)
    drakan_stage_webgpu(gpu_test)
    add_test(NAME gpu COMMAND gpu_test)
    # 77 = no adapter on this machine (headless CI without a software Vulkan driver).
    set_tests_properties(gpu PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()
