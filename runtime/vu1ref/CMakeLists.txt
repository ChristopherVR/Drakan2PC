# VU1 reference simulator and its replay / dump-test tools (replaces build.bat). C++17, strict FP (bit-exact reference).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_vu1ref LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(vu1ref)

add_library(drakan_vu1ref STATIC vu1ref.cpp)
add_library(drakan::vu1ref ALIAS drakan_vu1ref)
target_include_directories(drakan_vu1ref PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(drakan_vu1ref PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(drakan_vu1ref PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2> /EHsc /fp:strict)
else()
    target_compile_options(drakan_vu1ref PRIVATE -O2 -ffp-contract=off)
endif()
target_link_libraries(drakan_vu1ref PRIVATE drakan::options)

# Tools: they need a VU1 recording (see recorder_hook.txt) so they are not ctest tests.
if(DRAKAN_BUILD_RUNTIME_TESTS)
    foreach(tool vu1ref_replay vu1ref_dumptest)
        add_executable(${tool} ${tool}.cpp)
        target_link_libraries(${tool} PRIVATE drakan_vu1ref drakan::options)
        if(MSVC)
            target_compile_options(${tool} PRIVATE $<$<NOT:$<CONFIG:Debug>>:/O2> /EHsc /fp:strict)
        endif()
    endforeach()
endif()
