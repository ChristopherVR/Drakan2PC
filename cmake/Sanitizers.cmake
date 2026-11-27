# Opt-in runtime error checking for developer and CI builds (never for player builds).
#
#   -DDRAKAN_SANITIZE=address            AddressSanitizer: out-of-bounds, use-after-free, double free, stack overflow
#                                        of buffers. On Linux/clang it also runs LeakSanitizer at exit.
#   -DDRAKAN_SANITIZE=address;undefined  Adds UndefinedBehaviorSanitizer (signed overflow, bad shifts, misaligned or
#                                        null access). GCC/Clang only; MSVC has no UBSan and ignores "undefined".
#
# The flags are applied to every target in the tree, dependencies included: MSVC refuses to link ASan objects with
# non-ASan objects that use the STL (LNK2038 'annotate_vector' mismatch), so a per-target flag is not enough.
# Use the *-asan presets (CMakePresets.json) rather than setting this by hand.
include_guard(GLOBAL)

set(DRAKAN_SANITIZE "" CACHE STRING "Sanitizers for developer builds: empty, 'address' or 'address;undefined'")

if(NOT DRAKAN_SANITIZE)
    return()
endif()

foreach(sanitizer IN LISTS DRAKAN_SANITIZE)
    if(NOT sanitizer MATCHES "^(address|undefined)$")
        message(FATAL_ERROR "DRAKAN_SANITIZE: unknown sanitizer '${sanitizer}' (expected address and/or undefined)")
    endif()
endforeach()

if(MSVC)
    if(NOT "address" IN_LIST DRAKAN_SANITIZE)
        message(FATAL_ERROR "DRAKAN_SANITIZE: MSVC only supports 'address'")
    endif()
    if("undefined" IN_LIST DRAKAN_SANITIZE)
        message(WARNING "DRAKAN_SANITIZE: MSVC has no UndefinedBehaviorSanitizer; using AddressSanitizer only")
    endif()
    # /RTC (default in Debug) cannot be combined with ASan; the asan presets build RelWithDebInfo, which has none.
    # Incremental linking is ignored with ASan and only produces a warning, so turn it off.
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)

    # Executables built with /MD load clang_rt.asan_dynamic-x86_64.dll at startup. It ships next to cl.exe; copy it
    # beside the binaries so ctest and manual runs work without touching PATH.
    get_filename_component(drakan_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB drakan_asan_runtime "${drakan_msvc_bin_dir}/clang_rt.asan_dynamic-x86_64.dll")
    if(NOT drakan_asan_runtime)
        message(FATAL_ERROR "DRAKAN_SANITIZE: clang_rt.asan_dynamic-x86_64.dll not found in ${drakan_msvc_bin_dir}; "
            "install the 'C++ AddressSanitizer' component of Visual Studio")
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
            file(COPY ${drakan_asan_runtime} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${config}")
        endforeach()
    else()
        file(COPY ${drakan_asan_runtime} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
else()
    list(JOIN DRAKAN_SANITIZE "," drakan_sanitize_list)
    # Report every UB hit as a failure instead of printing and carrying on, so ctest goes red.
    set(drakan_sanitize_flags -fsanitize=${drakan_sanitize_list} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_compile_options(${drakan_sanitize_flags} -g)
    add_link_options(${drakan_sanitize_flags})
endif()

message(STATUS "Sanitizers enabled: ${DRAKAN_SANITIZE}")
