# PS2 guest runtime (ps2_runtime) and the game executable (ps2EntryRunner -> drakan.exe). Added by the root
# CMakeLists.txt; the Drakan runtime libraries (drakan::hle, drakan::gs, ...) are already defined there.
option(PS2X_ENABLE_RUNNER_UNITY_BUILD "Build ps2EntryRunner with CMake unity build" ON)
set(PS2X_RUNNER_UNITY_BUILD_BATCH_SIZE 32 CACHE STRING "Unity build batch size for ps2EntryRunner")
option(PS2X_ENABLE_RUNNER_PCH "Precompile the heavy runtime headers for ps2EntryRunner" ON)

option(PS2X_ENABLE_RUNTIME_LOGS "Enable PS2 runtime logs" ON)
option(PS2X_ENABLE_AGRESSIVE_LOGS "Enable very verbose/agressive PS2 runtime logs" ON)
option(PS2X_ENABLE_IOP_RPC_TRACE "Log unhandled IOP/SIF RPC trace suggestions" ON)
option(PS2X_STRICT_RETURN_DIAGNOSTICS "Route generated JR $ra returns through runtime branch diagnostics" OFF)
option(PS2X_SHOW_WINDOWS_CONSOLE "Show a console window for ps2EntryRunner on Windows release builds" ON)
option(PS2X_ENABLE_DEBUG_UI "Build the desktop runtime debug UI" ON)


set(PS2X_IS_VITA OFF)

if((CMAKE_C_COMPILER MATCHES "arm-vita-eabi") OR
    (CMAKE_CXX_COMPILER MATCHES "arm-vita-eabi"))
    set(PS2X_IS_VITA ON)
endif()

set(PS2X_IS_ANDROID OFF)

if(ANDROID)
    set(PS2X_IS_ANDROID ON)
endif()

option(PS2X_VITA_CREATE_PACKAGE "Create Vita eboot/vpk targets" ON)
set(PS2X_VITA_APP_NAME "PS2 Retro X" CACHE STRING "Display name for the Vita bubble")
set(PS2X_VITA_TITLEID "RANJ00001" CACHE STRING "9-character Vita title id")
set(PS2X_VITA_VERSION "01.00" CACHE STRING "Vita app version")
set(PS2X_DEFAULT_BOOT_ELF "" CACHE STRING "Guest ELF path passed directly to main() when argv is unavailable")

if(PS2X_IS_VITA)
    if(NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "VITASDK is not defined. Configure inside the VitaSDK environment or pass the Vita toolchain file.")
    endif()

    set(PS2X_VITASDK "$ENV{VITASDK}")

    if(NOT EXISTS "${PS2X_VITASDK}/share/vita.cmake")
        message(FATAL_ERROR "Could not find vita.cmake under ${PS2X_VITASDK}/share.")
    endif()

    include("${PS2X_VITASDK}/share/vita.cmake" REQUIRED)
else()
    include(FetchContent)
    set(FETCHCONTENT_QUIET FALSE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

    if(PS2X_IS_ANDROID)
        set(PLATFORM "Android" CACHE STRING "" FORCE)
    elseif(EMSCRIPTEN)
        set(PLATFORM "Web" CACHE STRING "" FORCE)
    endif()

    FetchContent_Declare(
        raylib
        GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
        GIT_TAG "5.5" # we will migrate to 4.2.0 later, trust me it will be better
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(raylib)

    # raylib's public ShowCursor/CloseWindow names collide with user32 exports at link time (LNK2005).
    # The runtime never calls raylib's ShowCursor (it uses the Win32 one), so rename it inside raylib only.
    if(MSVC AND TARGET raylib)
        target_compile_definitions(raylib PRIVATE ShowCursor=rlShowCursor)
    endif()

    if(PS2X_ENABLE_DEBUG_UI AND NOT PS2X_IS_ANDROID)
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG "v1.92.7-docking"
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(imgui)

        if(NOT imgui_POPULATED)
            FetchContent_Populate(imgui)
        endif()

        set(PS2X_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}")

        add_library(imgui STATIC
            "${PS2X_IMGUI_SOURCE_DIR}/imgui.cpp"
            "${PS2X_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
            "${PS2X_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
            "${PS2X_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
            "${PS2X_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
        )
        target_include_directories(imgui PUBLIC "${PS2X_IMGUI_SOURCE_DIR}")

        FetchContent_Declare(
            rlImGui
            GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
            GIT_TAG "Raylib_5_5"
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(rlImGui)

        if(NOT rlimgui_POPULATED)
            FetchContent_Populate(rlImGui)
        endif()

        set(PS2X_RLIMGUI_SOURCE_DIR "${rlimgui_SOURCE_DIR}")

        add_library(rlImGui STATIC
            "${PS2X_RLIMGUI_SOURCE_DIR}/rlImGui.cpp"
        )
        target_include_directories(rlImGui PUBLIC "${PS2X_RLIMGUI_SOURCE_DIR}")
        target_link_libraries(rlImGui PUBLIC raylib imgui)
    endif()
endif()

add_library(ps2_host_backend INTERFACE)

set(PS2X_DEFAULT_BOOT_ELF_DEFINE "")

if(PS2X_DEFAULT_BOOT_ELF)
    set(PS2X_DEFAULT_BOOT_ELF_DEFINE "${PS2X_DEFAULT_BOOT_ELF}")
    string(REPLACE "\\" "\\\\" PS2X_DEFAULT_BOOT_ELF_DEFINE "${PS2X_DEFAULT_BOOT_ELF_DEFINE}")
    string(REPLACE "\"" "\\\"" PS2X_DEFAULT_BOOT_ELF_DEFINE "${PS2X_DEFAULT_BOOT_ELF_DEFINE}")
endif()

if(PS2X_IS_VITA)
    set(PS2X_VITA_PREFIX "${PS2X_VITASDK}/arm-vita-eabi")
    set(PS2X_VITA_EXTRA_INCLUDE_DIRS "" CACHE STRING "Extra include directories for Vita host dependencies")
    set(PS2X_VITA_LIBRARY_DIR "${PS2X_VITA_PREFIX}/lib")
    set(PS2X_VITA_REQUIRED_LIBRARY_NAMES
        raylib
        SDL2
        OpenSLES
        taihen_stub
        SceAppMgr_stub
        SceCtrl_stub
        SceGxm_stub
        SceCommonDialog_stub
        SceLibKernel_stub
        SceAudio_stub
        SceTouch_stub
        SceHid_stub
        SceMotion_stub
        SceSysmodule_stub
        SceIofilemgr_stub
        SceNetCtl_stub
        SceNet_stub
        SceDisplay_stub
        SceAppUtil_stub
        SceAudioIn_stub
        ScePower_stub
        SceProcessmgr_stub
        SceIme_stub
        libIMGEGL_stub_weak
        libgpu_es4_ext_stub_weak
        libGLESv2_stub_weak
        libGL_stub_weak
        libGLESv1_CM_stub_weak
    )

    set(PS2X_VITA_INCLUDE_DIRS
        "${PS2X_VITA_PREFIX}/include"
    )

    foreach(PS2X_VITA_OPTIONAL_INCLUDE_DIR IN ITEMS
        "${PS2X_VITA_PREFIX}/include/raylib"
        "${PS2X_VITA_PREFIX}/include/SDL2")
        if(EXISTS "${PS2X_VITA_OPTIONAL_INCLUDE_DIR}")
            list(APPEND PS2X_VITA_INCLUDE_DIRS "${PS2X_VITA_OPTIONAL_INCLUDE_DIR}")
        endif()
    endforeach()

    if(PS2X_VITA_EXTRA_INCLUDE_DIRS)
        list(APPEND PS2X_VITA_INCLUDE_DIRS ${PS2X_VITA_EXTRA_INCLUDE_DIRS})
    endif()

    if(NOT EXISTS "${PS2X_VITA_PREFIX}/include/SDL2/SDL.h")
        message(FATAL_ERROR
            "Missing SDL2 headers under ${PS2X_VITA_PREFIX}/include/SDL2. "
            "Quenom/raylib-5.5-vita requires SDL2 with PVR support installed into VitaSDK.")
    endif()

    if(NOT EXISTS "${PS2X_VITA_LIBRARY_DIR}")
        message(FATAL_ERROR "Missing Vita library directory: ${PS2X_VITA_LIBRARY_DIR}")
    endif()

    function(ps2x_find_vita_library out_var library_name)
        string(MAKE_C_IDENTIFIER "${library_name}" library_id)
        set(cache_var "PS2X_VITA_LIB_${library_id}")

        find_library(${cache_var}
            NAMES "${library_name}" "${library_name}.a" "lib${library_name}.a"
            PATHS "${PS2X_VITA_LIBRARY_DIR}"
            NO_DEFAULT_PATH
        )

        if(NOT ${cache_var})
            message(FATAL_ERROR
                "Could not find Vita library '${library_name}' under ${PS2X_VITA_LIBRARY_DIR}. "
                "Install Quenom/raylib-5.5-vita and its SDL2/PVR dependencies into VitaSDK first.")
        endif()

        set(${out_var} "${${cache_var}}" PARENT_SCOPE)
    endfunction()

    set(PS2X_VITA_RESOLVED_LIBRARIES "")

    foreach(PS2X_VITA_LIBRARY_NAME IN LISTS PS2X_VITA_REQUIRED_LIBRARY_NAMES)
        ps2x_find_vita_library(PS2X_VITA_LIBRARY_PATH "${PS2X_VITA_LIBRARY_NAME}")
        list(APPEND PS2X_VITA_RESOLVED_LIBRARIES "${PS2X_VITA_LIBRARY_PATH}")
    endforeach()

    target_compile_definitions(ps2_host_backend INTERFACE PLATFORM_VITA)
    target_include_directories(ps2_host_backend INTERFACE ${PS2X_VITA_INCLUDE_DIRS})
    target_link_libraries(ps2_host_backend INTERFACE
        ${PS2X_VITA_RESOLVED_LIBRARIES}
        m
        c
        pthread
    )
else()
    target_link_libraries(ps2_host_backend INTERFACE raylib)

    if(PS2X_IS_ANDROID)
        target_link_libraries(ps2_host_backend INTERFACE log android)
    endif()
endif()

if(PS2X_IS_ANDROID)
    set(PS2X_ENABLE_FFMPEG_DEFAULT OFF)
else()
    set(PS2X_ENABLE_FFMPEG_DEFAULT ON)
endif()

option(PS2X_ENABLE_FFMPEG "Enable FFmpeg-backed MPEG video decoding" ${PS2X_ENABLE_FFMPEG_DEFAULT})

if(NOT PS2X_ENABLE_FFMPEG)
    message(STATUS "FFmpeg disabled; MPEG video decode falls back to stub frames")
    add_library(ffmpeg INTERFACE)
elseif(WIN32)
    include(ExternalProject)

    set(FFMPEG_PREBUILT_TAG "n7.1-241205")
    # FFMPEG_PREBUILT_URL may point at a local copy of the same archive (the offline builder kit passes one).
    if(NOT FFMPEG_PREBUILT_URL)
        set(FFMPEG_PREBUILT_URL
            "https://github.com/System233/ffmpeg-msvc-prebuilt/releases/download/${FFMPEG_PREBUILT_TAG}/ffmpeg-${FFMPEG_PREBUILT_TAG}-lgpl-amd64-shared.zip")
    endif()

    set(FFMPEG_PREFIX_DIR "${CMAKE_BINARY_DIR}/ThirdParty/ffmpeg-prefix")
    set(FFMPEG_SOURCE_DIR "${FFMPEG_PREFIX_DIR}/src/ffmpeg_external")
    set(FFMPEG_INCLUDE_DIR "${FFMPEG_SOURCE_DIR}/include")
    set(FFMPEG_LIB_DIR "${FFMPEG_SOURCE_DIR}/bin")
    set(FFMPEG_BIN_DIR "${FFMPEG_SOURCE_DIR}/bin")

    ExternalProject_Add(ffmpeg_external
        URL "${FFMPEG_PREBUILT_URL}"
        URL_HASH SHA256=b985887dbca8c0a9a4c47f9d2bac7ec3ab994335b1b7fe7000bdb1dcd772ac68
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX "${FFMPEG_PREFIX_DIR}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS
        "${FFMPEG_LIB_DIR}/avcodec.lib"
        "${FFMPEG_LIB_DIR}/avformat.lib"
        "${FFMPEG_LIB_DIR}/avutil.lib"
        "${FFMPEG_LIB_DIR}/swresample.lib"
        "${FFMPEG_LIB_DIR}/swscale.lib"
    )

    # Link against the import libraries shipped with the Windows DLLs.
    add_library(ffmpeg_avcodec STATIC IMPORTED GLOBAL)
    set_target_properties(ffmpeg_avcodec PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/avcodec.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    )
    add_dependencies(ffmpeg_avcodec ffmpeg_external)

    add_library(ffmpeg_avformat STATIC IMPORTED GLOBAL)
    set_target_properties(ffmpeg_avformat PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/avformat.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    )
    add_dependencies(ffmpeg_avformat ffmpeg_external)

    add_library(ffmpeg_avutil STATIC IMPORTED GLOBAL)
    set_target_properties(ffmpeg_avutil PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/avutil.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    )
    add_dependencies(ffmpeg_avutil ffmpeg_external)

    add_library(ffmpeg_swresample STATIC IMPORTED GLOBAL)
    set_target_properties(ffmpeg_swresample PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/swresample.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    )
    add_dependencies(ffmpeg_swresample ffmpeg_external)

    add_library(ffmpeg_swscale STATIC IMPORTED GLOBAL)
    set_target_properties(ffmpeg_swscale PROPERTIES
        IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/swscale.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIR}"
    )
    add_dependencies(ffmpeg_swscale ffmpeg_external)

    add_library(ffmpeg INTERFACE)
    target_link_libraries(ffmpeg INTERFACE
        ffmpeg_avcodec
        ffmpeg_avformat
        ffmpeg_avutil
        ffmpeg_swresample
        ffmpeg_swscale
        bcrypt
        secur32
        ws2_32
        user32
        advapi32
        ole32
        shell32
    )

    set(PS2X_FFMPEG_BIN_DIR "${FFMPEG_BIN_DIR}" CACHE INTERNAL "Directory containing FFmpeg runtime DLLs")
    set(PS2X_COPY_FFMPEG_DLLS_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyFfmpegDlls.cmake"
        CACHE INTERNAL
        "Script used to stage FFmpeg runtime DLLs beside executables")
elseif(EMSCRIPTEN)
    set(PS2X_WEB_FFMPEG_DIR "" CACHE PATH "Pinned Wasm FFmpeg install from prepare_web_deps.py")
    if(NOT EXISTS "${PS2X_WEB_FFMPEG_DIR}/lib/libavcodec.a")
        message(FATAL_ERROR "Run prepare_web_deps.py and set PS2X_WEB_FFMPEG_DIR")
    endif()
    add_library(ffmpeg INTERFACE)
    target_include_directories(ffmpeg INTERFACE "${PS2X_WEB_FFMPEG_DIR}/include")
    foreach(component avformat avcodec swscale avutil)
        target_link_libraries(ffmpeg INTERFACE "${PS2X_WEB_FFMPEG_DIR}/lib/lib${component}.a")
    endforeach()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET GLOBAL
        libavcodec
        libavformat
        libavutil
        libswresample
        libswscale
    )

    add_library(ffmpeg INTERFACE)
    target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

function(ps2x_stage_ffmpeg_runtime_dlls target_name)
    if(NOT WIN32)
        return()
    endif()

    if(NOT PS2X_FFMPEG_BIN_DIR OR NOT PS2X_COPY_FFMPEG_DLLS_SCRIPT)
        return()
    endif()

    if(TARGET ffmpeg_external)
        add_dependencies(${target_name} ffmpeg_external)
    endif()

    add_custom_command(
        TARGET ${target_name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
        "-Dsource_dir=${PS2X_FFMPEG_BIN_DIR}"
        "-Ddest_dir=$<TARGET_FILE_DIR:${target_name}>"
        -P "${PS2X_COPY_FFMPEG_DLLS_SCRIPT}"
        VERBATIM
    )
endfunction()

add_library(ps2_runtime STATIC
    src/lib/game_overrides.cpp
    src/lib/drakan_overrides.cpp
    src/lib/drakan_input_glue.cpp
    src/lib/drakan_config.cpp
    src/lib/drakan_pause.cpp
    src/lib/drakan_widescreen.cpp
    src/lib/drakan_subtitles.cpp
    src/lib/drakan_fpsmeter.cpp
    src/lib/drakan_menus.cpp
    src/lib/drakan_save_previews.cpp
    src/lib/drakan_log.cpp
    src/lib/drakan_crash.cpp
    src/lib/drakan_object_motion.cpp
    src/lib/drakan_dual_wield.cpp
    src/lib/drakan_region.cpp
    src/lib/gs/ps2_gif_arbiter.cpp
    src/lib/ps2_audio.cpp
    src/lib/ps2_audio_vag.cpp
    src/lib/gs/ps2_gs_memory.cpp
    src/lib/gs/gs_frontend.cpp
    src/lib/gs/gs_capture.cpp
    src/lib/gs/gs_cpu_backend.cpp
    src/lib/gs/gs_cpu_async_backend.cpp
    src/lib/ps2_iop_host.cpp
    src/lib/ps2_memory.cpp
    src/lib/ps2_ipu.cpp
    src/lib/ps2_pad.cpp
    src/lib/ps2_rom_device.cpp
    src/lib/ps2_vfs.cpp
    src/lib/ps2_runtime.cpp
    src/lib/ps2_perf.cpp
    src/lib/ps2_vif1_interpreter.cpp
    src/lib/vu/ps2_vu1_core.cpp
    src/lib/vu/ps2_vu1_upper.cpp
    src/lib/vu/ps2_vu1_lower.cpp
    src/lib/vu/ps2_vu1_check.cpp
    src/lib/vu/ps2_vu1_async.cpp
    src/lib/vu/ps2_vu1_batch.cpp
    src/lib/vu/ps2_vu1_native_kernels.cpp
    src/lib/vu/aot/ps2_vu1_aot.cpp
    src/lib/vu/aot/ps2_vu1_aot_0.cpp
    src/lib/vu/aot/ps2_vu1_aot_1.cpp
    src/lib/vu/aot/ps2_vu1_aot_2.cpp
    src/lib/vu/aot/ps2_vu1_aot_3.cpp
    src/lib/vu/aot/ps2_vu1_func.cpp
    src/lib/vu/ref/ps2_vu1_ref_core.cpp
    src/lib/vu/ref/ps2_vu1_ref_upper.cpp
    src/lib/vu/ref/ps2_vu1_ref_lower.cpp
    src/lib/games_database.cpp
)

# The software GS inherits guest rounding. Match the parity tests' numerical
# contract without changing release optimization for the rest of the runtime.
if(MSVC)
    set_property(SOURCE src/lib/gs/gs_cpu_backend.cpp APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
elseif(EMSCRIPTEN)
    set_property(SOURCE src/lib/gs/gs_cpu_backend.cpp APPEND PROPERTY COMPILE_OPTIONS -ffp-contract=off)
else()
    set_property(SOURCE src/lib/gs/gs_cpu_backend.cpp APPEND PROPERTY COMPILE_OPTIONS -frounding-math -ffp-contract=off)
endif()

# Player builds overlap CPU rendering with VU/GIF work. Library/legacy tests
# retain their synchronous default; PS2X_CPU_ASYNC can override either choice.
if(DRAKAN_BUILD_GAME AND NOT WIN32)
    set_source_files_properties(src/lib/gs/gs_cpu_async_backend.cpp PROPERTIES
        COMPILE_DEFINITIONS PS2X_CPU_ASYNC_DEFAULT=1)
endif()

# Keep platform services out of the common source list. Linux uses /proc/self/exe;
# the Windows implementation retains DPI/restart handling unchanged.
if(WIN32)
    target_sources(ps2_runtime PRIVATE src/lib/drakan_config_win.cpp)
else()
    target_sources(ps2_runtime PRIVATE src/lib/drakan_config_posix.cpp)
endif()
if(NOT TARGET drakan::gs)
    target_sources(ps2_runtime PRIVATE src/lib/gs/gs_gsr_unavailable.cpp)
endif()
find_package(Threads REQUIRED)
target_link_libraries(ps2_runtime PUBLIC Threads::Threads)
if(NOT MSVC)
    # Generated GPR access aliases the vector register file through scalar pointers.
    target_compile_options(ps2_runtime PUBLIC -fno-strict-aliasing)
    if(NOT EMSCRIPTEN AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
        target_compile_options(ps2_runtime PUBLIC -msse4.1)
    endif()
endif()

# VU1 interpreter / ahead-of-time compiled microcode: no stack cookies in the per-pair functions; the block tables of
# aot/ps2_vu1_blocks.h are built by the compiler (constant evaluation).
file(GLOB_RECURSE PS2X_VU_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/vu/*.cpp")
if(MSVC)
    set_source_files_properties(${PS2X_VU_SOURCES} PROPERTIES COMPILE_OPTIONS "/GS-;/constexpr:steps200000000;/constexpr:depth2048")
    target_compile_options(ps2_runtime PRIVATE /MP)
    # Each aot/ps2_vu1_aot_N.cpp slice takes ~17 GB and ~10 min in cl (constant evaluation), so four side by side in
    # one /MP batch need ~70 GB. A distinct define per slice gives each its own MSBuild CL batch: they then compile one
    # after another. Build-Drakan.ps1 (player builds) turns this on.
    option(PS2X_VU1_AOT_SERIAL "Compile the VU1 AOT slices one at a time to bound compiler memory" OFF)
    if(PS2X_VU1_AOT_SERIAL)
        foreach(slice 0 1 2 3)
            set_property(SOURCE src/lib/vu/aot/ps2_vu1_aot_${slice}.cpp APPEND PROPERTY COMPILE_DEFINITIONS
                PS2X_VU1_AOT_SLICE=${slice})
        endforeach()
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(${PS2X_VU_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-fconstexpr-steps=200000000;-fconstexpr-depth=2048;-fbracket-depth=2048")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(${PS2X_VU_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-fconstexpr-ops-limit=200000000;-fconstexpr-depth=2048")
endif()
target_link_libraries(ps2_runtime PRIVATE drakan::options)

if(PS2X_ENABLE_RUNTIME_LOGS)
    target_compile_definitions(ps2_runtime PUBLIC
        PS2_RUNTIME_LOGS=1
    )
endif()

if(PS2X_ENABLE_AGRESSIVE_LOGS)
    target_compile_definitions(ps2_runtime PUBLIC
        AGRESSIVE_LOGS=1
        PS2_FUNCTION_LOG_TRACKER=1
    )
endif()

if(PS2X_ENABLE_IOP_RPC_TRACE)
    target_compile_definitions(ps2_runtime PUBLIC
        PS2X_ENABLE_IOP_RPC_TRACE=1
    )
else()
    target_compile_definitions(ps2_runtime PUBLIC
        PS2X_ENABLE_IOP_RPC_TRACE=0
    )
endif()

if(PS2X_STRICT_RETURN_DIAGNOSTICS)
    target_compile_definitions(ps2_runtime PUBLIC
        PS2X_STRICT_RETURN_DIAGNOSTICS=1
    )
endif()

target_compile_definitions(ps2_runtime PRIVATE
    PS2X_HAS_FFMPEG=$<BOOL:${PS2X_ENABLE_FFMPEG}>
)

file(GLOB_RECURSE KERNEL_SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/Kernel/*.cpp"
)

target_sources(ps2_runtime PRIVATE
    ${KERNEL_SRC_FILES}
)

file(GLOB RUNNER_SRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/runner/*.cpp"
)

# Drakan: build the recompiler output (batched translation units) into a separate library, without the
# runner's /GL + LTCG fast-release flags (whole-program optimisation over 100+ MB of generated code is unusable).
set(PS2X_OUTPUT_DIR "" CACHE PATH "Directory for drakan.exe (default: <generated dir>/../bin)")
set(PS2X_GENERATED_DIR "" CACHE PATH "Directory holding recompiler output (ps2_recomp_*.cpp, register_functions.cpp, headers)")
set(PS2X_GENERATED_OPT "/O2" CACHE STRING "MSVC optimisation flag for generated code (/O1 or /O2)")
# Disc region of the recompiled executable (docs/reference/regions.md). PAL adds no definition, so a PAL build compiles
# exactly as before; USA selects the region profile in drakan_region.h. PUBLIC so every target that can include
# drakan_region.h agrees on it (one region per WorkDir: prepare_iso.py records it in local-input.json).
set(DRAKAN_GAME_REGION "pal" CACHE STRING "Disc region of the recompiled executable: pal (SCES_500.06) or usa (SCUS_971.28)")
set_property(CACHE DRAKAN_GAME_REGION PROPERTY STRINGS pal usa)
string(TOLOWER "${DRAKAN_GAME_REGION}" DRAKAN_GAME_REGION_LOWER)
if(DRAKAN_GAME_REGION_LOWER STREQUAL "pal")
    set(DRAKAN_BOOT_ELF_NAME "SCES_500.06")
elseif(DRAKAN_GAME_REGION_LOWER STREQUAL "usa")
    set(DRAKAN_BOOT_ELF_NAME "SCUS_971.28")
    target_compile_definitions(ps2_runtime PUBLIC DRAKAN_GAME_REGION_USA=1)
else()
    message(FATAL_ERROR "DRAKAN_GAME_REGION must be pal or usa, not '${DRAKAN_GAME_REGION}'")
endif()
if(PS2X_GENERATED_DIR)
    list(FILTER RUNNER_SRC_FILES EXCLUDE REGEX "register_functions[.]cpp$")
endif()

set(RUNNER_MAIN_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/runner/main.cpp")
set(ROOT_MAIN_CPP "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

if(EXISTS "${RUNNER_MAIN_CPP}")
    list(APPEND RUNNER_SRC_FILES "${RUNNER_MAIN_CPP}")
elseif(EXISTS "${ROOT_MAIN_CPP}")
    list(APPEND RUNNER_SRC_FILES "${ROOT_MAIN_CPP}")
endif()

if(PS2X_IS_ANDROID)
    add_library(ps2EntryRunner SHARED
        ${RUNNER_SRC_FILES}
    )

    target_link_options(ps2EntryRunner PRIVATE "-Wl,--undefined=ANativeActivity_onCreate")
else()
    add_executable(ps2EntryRunner
        ${RUNNER_SRC_FILES}
    )
    if(WIN32 AND MSVC)
        target_sources(ps2EntryRunner PRIVATE src/res/drakan.rc)
        # Drakan: no console window (drakan_log.cpp redirects stdout / stderr to drakan.log; [general] console = 1 opens one)
        option(PS2X_DRAKAN_GUI_SUBSYSTEM "Link ps2EntryRunner as a Windows GUI app" ON)
        if(PS2X_DRAKAN_GUI_SUBSYSTEM)
            target_link_options(ps2EntryRunner PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
        endif()
    endif()
endif()

if(PS2X_OUTPUT_DIR)
    set(PS2X_OUTPUT_DIR_EFFECTIVE "${PS2X_OUTPUT_DIR}")
else()
    set(PS2X_OUTPUT_DIR_EFFECTIVE "${PS2X_GENERATED_DIR}/../bin")
endif()
if(PS2X_GENERATED_DIR)
    file(GLOB PS2X_GENERATED_SOURCES CONFIGURE_DEPENDS "${PS2X_GENERATED_DIR}/*.cpp")
    if(WIN32)
        add_library(ps2_generated STATIC ${PS2X_GENERATED_SOURCES})
    else()
        # The generated registration table and the runtime reference each other.
        # Object inclusion also keeps all registration constructors visible to
        # ELF/wasm LTO, instead of discovering native objects after internalization.
        add_library(ps2_generated OBJECT ${PS2X_GENERATED_SOURCES})
    endif()
    target_include_directories(ps2_generated PRIVATE "${PS2X_GENERATED_DIR}")
    target_link_libraries(ps2_generated PUBLIC ps2_runtime)
    target_link_libraries(ps2EntryRunner PRIVATE ps2_generated)
    if(MSVC)
        target_compile_options(ps2_generated PRIVATE /MP /FS ${PS2X_GENERATED_OPT} /GS- /Gy /arch:AVX2 /bigobj)
        target_link_options(ps2EntryRunner PRIVATE /STACK:268435456 /MAP)
    endif()
    set_target_properties(ps2EntryRunner PROPERTIES
        OUTPUT_NAME drakan
        RUNTIME_OUTPUT_DIRECTORY "${PS2X_OUTPUT_DIR_EFFECTIVE}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PS2X_OUTPUT_DIR_EFFECTIVE}")
endif()

if(NOT PS2X_GENERATED_DIR)
    # Without recompiler output the runner cannot link; it stays buildable on request (unit tests only need ps2_runtime).
    set_target_properties(ps2EntryRunner PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

if(PS2X_ENABLE_RUNNER_UNITY_BUILD)
    set_target_properties(ps2EntryRunner PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE "${PS2X_RUNNER_UNITY_BUILD_BATCH_SIZE}"
    )
endif()

if(PS2X_ENABLE_RUNNER_PCH)
    set(PS2X_RUNNER_PCH_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2_runtime_macros.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2_runtime.h"
    )

    foreach(PS2X_RUNNER_GENERATED_HEADER IN ITEMS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2_recompiled_functions.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2_recompiled_stubs.h")
        if(EXISTS "${PS2X_RUNNER_GENERATED_HEADER}")
            list(APPEND PS2X_RUNNER_PCH_HEADERS "${PS2X_RUNNER_GENERATED_HEADER}")
        endif()
    endforeach()

    list(APPEND PS2X_RUNNER_PCH_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2_syscalls.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2_stubs.h"
    )

    target_precompile_headers(ps2EntryRunner PRIVATE ${PS2X_RUNNER_PCH_HEADERS})
endif()

if(PS2X_DEFAULT_BOOT_ELF_DEFINE)
    target_compile_definitions(ps2EntryRunner PRIVATE
        PS2X_DEFAULT_BOOT_ELF="${PS2X_DEFAULT_BOOT_ELF_DEFINE}"
    )
endif()

if(MSVC)
    target_compile_options(ps2EntryRunner PRIVATE /FS)
endif()

target_include_directories(ps2_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/Kernel
)

# Drakan runtime libraries (runtime/<lib>), consumed by target. The root CMakeLists.txt adds them before this directory.
# Object-motion job metadata is standard C++, also used by generated/runtime headers and non-Windows executors;
# drakan::gs_headers adds headers, not a GPU dependency.
target_link_libraries(ps2_runtime PUBLIC drakan::gs_headers PRIVATE drakan::texture)
foreach(lib IN ITEMS fmv hle input)
    target_link_libraries(ps2_runtime PRIVATE drakan::${lib})
endforeach()
target_link_libraries(ps2_runtime PRIVATE drakan::ui drakan::gameui)
target_compile_definitions(ps2_runtime PRIVATE PS2X_HAVE_UI=1)

if(TARGET drakan::spu2)
    target_link_libraries(ps2_runtime PRIVATE drakan::spu2)
    target_compile_definitions(ps2_runtime PRIVATE PS2X_HAVE_SPU2=1)
endif()

if(TARGET drakan::gs)
    # The game window, the GS renderer ("gsr": Direct3D 11 or WebGPU on Windows, WebGPU on Linux), the mouse + keyboard
    # + gamepad input module and the in-game PC settings overlay (F1 / Options > "PC Settings"), shared by Windows and
    # Linux. Only the window / input host differs: Win32 (drakan_window.cpp, Raw Input) or GLFW (drakan_window_glfw.cpp,
    # drakan_input_glfw.cpp; raylib's GLFW).
    target_sources(ps2_runtime PRIVATE src/lib/gs/gs_gsr_bridge.cpp src/lib/drakan_dlss.cpp
        src/lib/drakan_scene_camera.cpp src/lib/drakan_settings_host.cpp src/lib/drakan_fmv_hd.cpp)
    target_link_libraries(ps2_runtime PRIVATE drakan::gs drakan::settings)
    target_compile_definitions(ps2_runtime PRIVATE PS2X_HAVE_DRAKAN_INPUT=1 PS2X_HAVE_GSR=1
        PS2X_HAVE_DRAKAN_SETTINGS=1)
    if(WIN32)
        target_sources(ps2_runtime PRIVATE src/lib/drakan_window.cpp)
        if(GSR_WEBGPU_SELECTED)
            # Both GS renderers are linked: PC Settings offers [video] renderer (WebGPU / Direct3D 11).
            set_property(SOURCE src/lib/drakan_settings_host.cpp APPEND PROPERTY COMPILE_DEFINITIONS
                PS2X_GSR_RENDERER_CHOICE=1)
        endif()
    elseif(EMSCRIPTEN)
        # The page's canvas and DOM events (player.js); the overlay is event-fed (drakan_settings_events).
        target_sources(ps2_runtime PRIVATE src/lib/drakan_window_web.cpp src/lib/drakan_input_web.cpp)
    else()
        target_sources(ps2_runtime PRIVATE src/lib/drakan_window_glfw.cpp src/lib/drakan_input_glfw.cpp)
        target_include_directories(ps2_runtime PRIVATE $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
        drakan_settings_glfw_overlay()
        target_link_libraries(ps2_runtime PRIVATE drakan_settings_glfw)
    endif()
else()
    # No GS renderer (browser): the raylib window with the software GS and the portable camera / UI.
    target_sources(ps2_runtime PRIVATE src/lib/drakan_portable_camera.cpp src/lib/drakan_portable_ui.cpp)
    target_link_libraries(ps2_runtime PRIVATE drakan::settings)
    set_property(SOURCE src/lib/drakan_input_glue.cpp src/lib/drakan_menus.cpp src/lib/drakan_portable_camera.cpp
        src/lib/drakan_overrides.cpp src/lib/drakan_pause.cpp src/lib/drakan_portable_ui.cpp src/lib/ps2_pad.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS PS2X_HAVE_PORTABLE_UI=1)
endif()

if(EMSCRIPTEN)
    # Decoder headers are private. Changing the decoder build must not rebuild
    # all game translation units, which never include FFmpeg headers.
    target_link_libraries(ps2_runtime PUBLIC ps2_host_backend ps2_iop PRIVATE ffmpeg)
else()
    target_link_libraries(ps2_runtime PUBLIC ps2_host_backend ffmpeg ps2_iop)
endif()
drakan_attach_portable_gpu(ps2_runtime)
drakan_stage_portable_gpu(ps2EntryRunner)
target_link_libraries(ps2EntryRunner
    PRIVATE
    ps2_runtime
)

if(EMSCRIPTEN)
    # pre.js reads the boot ELF name from here (/SCES_500.06 for PAL, /SCUS_971.28 for USA).
    set(DRAKAN_WEB_REGION_JS "${CMAKE_CURRENT_BINARY_DIR}/drakan_web_region.js")
    file(GENERATE OUTPUT "${DRAKAN_WEB_REGION_JS}"
        CONTENT "Module['drakanBootElf'] = '/${DRAKAN_BOOT_ELF_NAME}';\n")
    target_compile_options(raylib PRIVATE -include "${CMAKE_SOURCE_DIR}/runtime/web/raylib_proxy.h")
    target_sources(ps2_runtime PRIVATE src/lib/drakan_web.cpp)
    set_property(TARGET ps2EntryRunner APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_SOURCE_DIR}/runtime/web/pre.js" "${CMAKE_SOURCE_DIR}/runtime/web/glfw_proxy.js")
    target_link_options(ps2EntryRunner PRIVATE -pthread -fwasm-exceptions
        "-sPROXY_TO_PTHREAD=1" "-sPTHREAD_POOL_SIZE=24"
        "-sDEFAULT_PTHREAD_STACK_SIZE=16777216" "-sTOTAL_STACK=16777216"
        "-sINITIAL_MEMORY=1073741824" "-sMAXIMUM_MEMORY=2147483648" "-sALLOW_MEMORY_GROWTH=1"
        "-sALLOW_BLOCKING_ON_MAIN_THREAD=0"
        "-sFORCE_FILESYSTEM=1" "-sEXIT_RUNTIME=1" "-sUSE_GLFW=3" "-sOFFSCREEN_FRAMEBUFFER=1"
        # The GS renderer's GPU thread owns #canvas as an OffscreenCanvas (rhi_proxy.cpp).
        "-sOFFSCREENCANVAS_SUPPORT=1" "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
        "-sEXPORTED_RUNTIME_METHODS=['FS','ENV']" "--emit-symbol-map"
        "--pre-js=${DRAKAN_WEB_REGION_JS}"
        "--pre-js=${DRAKAN_ROOT_DIR}/runtime/web/pre.js"
        "--js-library=${DRAKAN_ROOT_DIR}/runtime/web/glfw_proxy.js"
        "--preload-file=${PS2X_GENERATED_DIR}/../${DRAKAN_BOOT_ELF_NAME}@/${DRAKAN_BOOT_ELF_NAME}" -lidbfs.js)
endif()

if(PS2X_ENABLE_DEBUG_UI AND NOT PS2X_IS_VITA AND NOT PS2X_IS_ANDROID)
    target_sources(ps2EntryRunner PRIVATE
        src/lib/ps2_debug_panel.cpp
    )
    target_link_libraries(ps2EntryRunner PRIVATE rlImGui imgui)
    target_compile_definitions(ps2EntryRunner PRIVATE PS2X_ENABLE_DEBUG_UI=1)
endif()

ps2x_stage_ffmpeg_runtime_dlls(ps2EntryRunner)

if(PS2X_IS_VITA)
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1") 
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")

    if(PS2X_VITA_CREATE_PACKAGE)
        set(PS2X_VITA_VPK_FILES "")

        foreach(PS2X_VITA_MODULE IN ITEMS
            libgpu_es4_ext.suprx
            libIMGEGL.suprx
            libGLESv2.suprx
            libpvrPSP2_WSEGL.suprx
            libGL.suprx)
            set(PS2X_VITA_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vita/module/${PS2X_VITA_MODULE}")

            if(EXISTS "${PS2X_VITA_MODULE_PATH}")
                list(APPEND PS2X_VITA_VPK_FILES FILE "${PS2X_VITA_MODULE_PATH}" "module/${PS2X_VITA_MODULE}")
            else()
                message(WARNING
                    "Missing ${PS2X_VITA_MODULE_PATH}; the vpk will rely on ur0:data/external/ for it")
            endif()
        endforeach()

        vita_create_self(eboot.bin ps2EntryRunner UNSAFE)
        vita_create_vpk(ps2EntryRunner.vpk ${PS2X_VITA_TITLEID} eboot.bin
            VERSION ${PS2X_VITA_VERSION}
            NAME ${PS2X_VITA_APP_NAME}
            ${PS2X_VITA_VPK_FILES}
        )
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    EnableFastReleaseMode(ps2_runtime)
    EnableFastReleaseMode(ps2EntryRunner)

    if(WIN32 AND NOT PS2X_SHOW_WINDOWS_CONSOLE)
        if(MSVC)
            target_link_options(ps2EntryRunner PRIVATE
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/SUBSYSTEM:WINDOWS>
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/ENTRY:mainCRTStartup>
            )
        elseif(MINGW)
            target_link_options(ps2EntryRunner PRIVATE
                $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-mwindows>
            )
        endif()
    endif()
endif()

# Vita: Work around for GCC 10's LTO plugin
if(PS2X_IS_VITA)
    target_link_options(ps2EntryRunner PRIVATE "-Wl,--allow-multiple-definition")
    target_sources(ps2EntryRunner PRIVATE src/lib/ps2_vita_runtime.cpp)
    set_source_files_properties(src/lib/ps2_vita_runtime.cpp PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION TRUE
        COMPILE_OPTIONS "-fno-lto"
    )
endif()

if(PS2X_IS_ANDROID)
    target_sources(ps2EntryRunner PRIVATE src/lib/ps2_android_runtime.cpp)
endif()

install(TARGETS ps2_runtime ps2EntryRunner
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(WIN32 AND PS2X_ENABLE_FFMPEG)
    install(DIRECTORY "${FFMPEG_BIN_DIR}/"
        DESTINATION bin
        FILES_MATCHING
        PATTERN "*.dll"
    )
endif()

install(DIRECTORY include/
    DESTINATION include
)
