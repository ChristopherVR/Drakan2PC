# RSRCIOP HLE + ISO9660 reader (drakan::hle). Platform free.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_hle LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(hle)

add_library(drakan_hle STATIC
    src/IsoFs.cpp
    src/FileService.cpp
    src/ArenaLevel.cpp
    src/ResourceService.cpp
    src/ResourceOverride.cpp
    src/ModPack.cpp
    src/SaveRecord.cpp
    src/SoundService.cpp)
add_library(drakan::hle ALIAS drakan_hle)
if(EMSCRIPTEN)
    target_sources(drakan_hle PRIVATE src/IsoSourceWeb.cpp)
else()
    target_sources(drakan_hle PRIVATE src/IsoSourceFile.cpp)
endif()
target_include_directories(drakan_hle PUBLIC include)
target_link_libraries(drakan_hle PRIVATE drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(arena_level_test tests/arena_level_test.cpp)
    target_link_libraries(arena_level_test PRIVATE drakan_hle drakan::options)
    drakan_enable_asserts(arena_level_test)
    add_test(NAME arena_level_test COMMAND arena_level_test)
    add_executable(iso_source_test tests/iso_source_test.cpp)
    target_link_libraries(iso_source_test PRIVATE drakan_hle drakan::options)
    drakan_enable_asserts(iso_source_test)
    add_test(NAME iso_source COMMAND iso_source_test "${CMAKE_CURRENT_BINARY_DIR}/synthetic.iso")
    add_executable(hle_test tests/hle_test.cpp)
    target_link_libraries(hle_test PRIVATE drakan_hle drakan::options)
    target_compile_definitions(hle_test PRIVATE HLE_TEST_EXPECTED="${CMAKE_CURRENT_SOURCE_DIR}/tests/expected.txt")
    add_executable(mod_test tests/mod_test.cpp)
    target_link_libraries(mod_test PRIVATE drakan_hle drakan::options)
    add_executable(save_record_test tests/save_record_test.cpp)
    target_link_libraries(save_record_test PRIVATE drakan_hle drakan::options)
    add_test(NAME save_record_test COMMAND save_record_test)
    add_executable(override_test tests/override_test.cpp)
    target_link_libraries(override_test PRIVATE drakan_hle drakan::options)
    add_executable(resource_sequence_test tests/resource_sequence_test.cpp)
    target_link_libraries(resource_sequence_test PRIVATE drakan_hle drakan::options)
    add_test(NAME resource_sequence_test COMMAND resource_sequence_test)
    # hle_test needs a disc image (DRAKAN_ISO env var or argv[1]); it exits 77 (skipped) without one.
    add_test(NAME hle_test COMMAND hle_test)
    set_tests_properties(hle_test PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME override_test COMMAND override_test)
    add_test(NAME mod_test COMMAND mod_test)
endif()
