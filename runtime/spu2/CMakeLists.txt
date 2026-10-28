# SPU2 + libsd + native output (drakan::spu2): WASAPI on Windows, ALSA on Linux.
# hle::logf and the ISpu2 interface come from drakan::hle.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_spu2 LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(spu2)
drakan_component_require(hle)

add_library(drakan_spu2 STATIC
    src/Spu2.cpp src/Mixer.cpp src/Adpcm.cpp src/Sdr.cpp src/AudioOutput.cpp src/LibsdPresets.cpp)
add_library(drakan::spu2 ALIAS drakan_spu2)
target_include_directories(drakan_spu2 PUBLIC include)
if(WIN32)
    target_sources(drakan_spu2 PRIVATE src/AudioOutputWasapi.cpp)
    target_link_libraries(drakan_spu2 PUBLIC ole32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA REQUIRED)
    target_sources(drakan_spu2 PRIVATE src/AudioOutputAlsa.cpp)
    target_link_libraries(drakan_spu2 PRIVATE ALSA::ALSA)
elseif(EMSCRIPTEN)
    target_sources(drakan_spu2 PRIVATE src/AudioOutputWeb.cpp)
endif()
find_package(Threads REQUIRED)
target_link_libraries(drakan_spu2 PRIVATE Threads::Threads)
target_compile_definitions(drakan_spu2 PRIVATE $<$<BOOL:${MSVC}>:NOMINMAX>)
target_link_libraries(drakan_spu2 PUBLIC drakan::hle PRIVATE drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(audio_output_test tests/audio_output_test.cpp)
    target_link_libraries(audio_output_test PRIVATE drakan_spu2 drakan::options)
    drakan_enable_asserts(audio_output_test)
    add_test(NAME audio_output COMMAND audio_output_test)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME audio_output_alsa_null COMMAND audio_output_test --alsa-null)
    endif()
    add_executable(libsd_test tests/libsd_test.cpp)
    target_link_libraries(libsd_test PRIVATE drakan_spu2)
    drakan_enable_asserts(libsd_test)
    add_test(NAME libsd COMMAND libsd_test)
    add_executable(spu2_test tests/spu2_test.cpp)
    target_link_libraries(spu2_test PRIVATE drakan_spu2 drakan::options)
    target_compile_definitions(spu2_test PRIVATE
        SPU2_DEFAULT_SDB="${DRAKAN_ROOT_DIR}/iso/sample/ENGLISH/COMMON/SOUNDS/SOUND.SDB"
        SPU2_DEFAULT_LIBSD="${DRAKAN_ROOT_DIR}/iso/IOP/MODULES/LIBSD.IRX"
        SPU2_DEFAULT_OUT="${CMAKE_CURRENT_BINARY_DIR}/test-output"
        SPU2_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
        SPU2_TEST_PYTHON="${Python3_EXECUTABLE}"
        $<$<BOOL:${MSVC}>:NOMINMAX>)
    add_test(NAME spu2_test COMMAND spu2_test)
    # Needs SOUND.SDB from the disc (iso/sample/... or --sdb); exits 77 (skipped) without it.
    set_tests_properties(spu2_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
