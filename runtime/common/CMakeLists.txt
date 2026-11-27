# Header-only helpers shared by the runtime libraries (drakan::common): ASCII text (common/Text.h) and UTF-8 paths
# (common/Path.h). No dependencies, platform free; link it PRIVATE wherever a helper is used.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_common LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(common)

add_library(drakan_common INTERFACE)
add_library(drakan::common ALIAS drakan_common)
target_include_directories(drakan_common INTERFACE include)
target_compile_features(drakan_common INTERFACE cxx_std_20)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(common_test tests/common_test.cpp)
    target_link_libraries(common_test PRIVATE drakan_common drakan::options)
    add_test(NAME common_test COMMAND common_test)
endif()
