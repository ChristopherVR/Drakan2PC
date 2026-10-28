# Drakan: The Ancients' Gates - PC port. Single CMake root for everything we build:
#
#   ps2recomp/ps2xRecomp   the MIPS -> C++ recompiler (ps2_recomp)         DRAKAN_BUILD_RECOMPILER
#   runtime/<lib>          Drakan runtime libraries + their tests         DRAKAN_BUILD_RUNTIME_TESTS / _GAME
#   scripts/tests          C++ policy tests (pacing, scheduler, ...)      DRAKAN_BUILD_RUNTIME_TESTS
#   ps2recomp/ps2xRuntime  guest runtime + drakan.exe                     DRAKAN_BUILD_GAME
#   ps2recomp/ps2xTest     PS2 runtime unit tests (heavy: raylib/FFmpeg)  DRAKAN_BUILD_PS2X_TESTS
#
# Use the presets (CMakePresets.json) rather than spelling the options out; see BUILDING.md.
cmake_minimum_required(VERSION 3.21)

# VitaSDK: pick up its toolchain file before project() when building inside the SDK environment.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VITASDK})
    set(PS2X_VITA_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake")
    if(EXISTS "${PS2X_VITA_TOOLCHAIN_FILE}")
        set(CMAKE_TOOLCHAIN_FILE "${PS2X_VITA_TOOLCHAIN_FILE}" CACHE PATH
            "Toolchain file used for cross-compiling" FORCE)
        message(STATUS "Using VitaSDK toolchain from VITASDK: ${CMAKE_TOOLCHAIN_FILE}")
    endif()
endif()

project(Drakan VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DRAKAN_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(DRAKAN_PS2RECOMP_DIR "${DRAKAN_ROOT_DIR}/ps2recomp")

option(DRAKAN_BUILD_RECOMPILER "Build the PS2 recompiler (ps2_recomp)" ON)
option(DRAKAN_BUILD_RUNTIME_TESTS "Build the runtime libraries (runtime/*) with their tests and the policy tests; no game data needed" ON)
option(DRAKAN_BUILD_GAME "Build drakan.exe; needs PS2X_GENERATED_DIR and the local files from scripts/prepare_local_runtime.py" OFF)
option(DRAKAN_BUILD_PS2X_TESTS "Build the PS2 runtime unit tests in ps2recomp/ps2xTest (pulls in raylib and FFmpeg)" OFF)

if(ANDROID)
    message(STATUS "Android target detected, building runtime only")
    set(DRAKAN_BUILD_RECOMPILER OFF)
    set(DRAKAN_BUILD_RUNTIME_TESTS OFF)
    set(DRAKAN_BUILD_PS2X_TESTS OFF)
endif()

# Dependencies ship their own test suites behind BUILD_TESTING; ours are selected by the DRAKAN_BUILD_* options.
set(BUILD_TESTING OFF CACHE BOOL "Third-party test suites (Drakan tests use DRAKAN_BUILD_*_TESTS)" FORCE)

# Every executable lands in <build>/bin[/<config>]; ctest, scripts and CI rely on this.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(CompilerOptions)
include(ReleaseMode)
include(WebPlatform)
include(Ps2xPlatform)

option(PS2X_ENABLE_SCCACHE "Use sccache as compiler launcher when available (Makefile / Ninja generators)" ON)
if(PS2X_ENABLE_SCCACHE)
    find_program(PS2X_SCCACHE_PROGRAM sccache)

    if(PS2X_SCCACHE_PROGRAM)
        if(CMAKE_C_COMPILER)
            set(CMAKE_C_COMPILER_LAUNCHER "${PS2X_SCCACHE_PROGRAM}")
        endif()

        set(CMAKE_CXX_COMPILER_LAUNCHER "${PS2X_SCCACHE_PROGRAM}")
        message(STATUS "Using sccache: ${PS2X_SCCACHE_PROGRAM}")
    else()
        message(STATUS "sccache not found; continuing without compiler launcher")
    endif()
endif()

enable_testing()

set(DRAKAN_NEED_RUNTIME_LIBS OFF)
if(DRAKAN_BUILD_RUNTIME_TESTS OR DRAKAN_BUILD_GAME OR DRAKAN_BUILD_PS2X_TESTS)
    set(DRAKAN_NEED_RUNTIME_LIBS ON)
endif()

if(DRAKAN_BUILD_GAME)
    set(PS2X_GENERATED_DIR "" CACHE PATH "Directory holding recompiler output (ps2_recomp_*.cpp, register_functions.cpp, headers)")
    if(NOT PS2X_GENERATED_DIR)
        message(FATAL_ERROR "DRAKAN_BUILD_GAME needs -DPS2X_GENERATED_DIR=<recompiler output>; "
            "pwsh scripts/buildtool/Build-LocalGame.ps1 -IsoPath <iso> does the whole pipeline")
    endif()
    foreach(local_file ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc ps2xRuntime/src/res/drakan.ico)
        if(NOT EXISTS "${DRAKAN_PS2RECOMP_DIR}/${local_file}")
            message(FATAL_ERROR "Missing local file ps2recomp/${local_file}; run scripts/prepare_local_runtime.py --elf <SCES_500.06>")
        endif()
    endforeach()
endif()

if(WIN32 AND MSVC AND (DRAKAN_BUILD_GAME OR DRAKAN_BUILD_PS2X_TESTS))
    enable_language(RC) # application icon (ps2xRuntime/src/res/drakan.rc)
endif()

if(DRAKAN_BUILD_RECOMPILER)
    add_subdirectory(ps2recomp/ps2xRecomp)
endif()

if(DRAKAN_NEED_RUNTIME_LIBS)
    add_subdirectory(third_party/imgui)
    add_subdirectory(runtime/hle)
    add_subdirectory(runtime/fmv)
    add_subdirectory(runtime/ui)
    add_subdirectory(runtime/gameui)   # modern Tab screen core (platform free)
    add_subdirectory(runtime/vu1ref)
    add_subdirectory(runtime/gs)       # defines drakan::gs_headers everywhere, drakan::gs on Windows
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR EMSCRIPTEN)
        add_subdirectory(runtime/spu2)
    endif()
    add_subdirectory(runtime/input)    # portable camera/controls core; Raw Input host remains Windows-only
    add_subdirectory(runtime/settings)
endif()

if(DRAKAN_BUILD_GAME OR DRAKAN_BUILD_PS2X_TESTS)
    add_subdirectory(ps2recomp/ps2xIOP)
    add_subdirectory(ps2recomp/ps2xRuntime)
endif()

if(DRAKAN_BUILD_PS2X_TESTS)
    add_subdirectory(ps2recomp/ps2xTest)
endif()

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_subdirectory(scripts/tests)
endif()
