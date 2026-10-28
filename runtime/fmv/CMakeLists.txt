# Native IPU/IDEC movie frame decoder (drakan::fmv). Platform free.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_fmv LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(fmv)

add_library(drakan_fmv STATIC src/fmv.cpp)
add_library(drakan::fmv ALIAS drakan_fmv)
target_include_directories(drakan_fmv PUBLIC include)
target_link_libraries(drakan_fmv PRIVATE drakan::options)
if(MSVC)
    target_compile_options(drakan_fmv PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2>)
endif()

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(fmv_test tests/fmv_test.cpp)
    target_link_libraries(fmv_test PRIVATE drakan_fmv drakan::options)
    add_test(NAME fmv_synthetic COMMAND fmv_test --synthetic)
    # Decodes a real movie; needs FMV_VDB=<path to a .VDB> (see fmv_test.cpp), exits 77 (skipped) without it.
    add_test(NAME fmv_vdb COMMAND fmv_test --from-env)
    set_tests_properties(fmv_vdb PROPERTIES SKIP_RETURN_CODE 77)
endif()
