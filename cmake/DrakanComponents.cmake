# Helpers shared by the runtime/<lib> components.
#
# Every runtime/<lib>/CMakeLists.txt can be add_subdirectory()'d from the repository root (the normal case) or
# configured on its own (`cmake -S runtime/<lib> -B build/<lib>`). `drakan_component_begin()` hides the difference.

# drakan_component_begin(<name>) - call first thing in runtime/<lib>/CMakeLists.txt, before any target is defined.
# Standalone (the caller already did cmake_minimum_required + project()): language standard, flags and testing.
# From the root: does nothing.
macro(drakan_component_begin name)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(CMAKE_CXX_STANDARD 20)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        get_filename_component(DRAKAN_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        set(DRAKAN_PS2RECOMP_DIR "${DRAKAN_ROOT_DIR}/ps2recomp")
        list(APPEND CMAKE_MODULE_PATH "${DRAKAN_ROOT_DIR}/cmake")
        include(CompilerOptions)
        option(DRAKAN_BUILD_RUNTIME_TESTS "Build the runtime library tests" ON)
        enable_testing()
    endif()
endmacro()

# drakan_component_require(<name>...) - make sure runtime/<name> (or third_party/imgui) is part of the build.
# From the root the component was already added; standalone it is pulled in on demand.
macro(drakan_component_require)
    foreach(_drakan_dep ${ARGN})
        if(NOT TARGET drakan::${_drakan_dep})
            if(_drakan_dep STREQUAL "imgui")
                set(_drakan_dep_dir "${DRAKAN_ROOT_DIR}/third_party/imgui")
            else()
                set(_drakan_dep_dir "${DRAKAN_ROOT_DIR}/runtime/${_drakan_dep}")
            endif()
            add_subdirectory("${_drakan_dep_dir}" "${CMAKE_BINARY_DIR}/_deps/${_drakan_dep}")
        endif()
    endforeach()
endmacro()
