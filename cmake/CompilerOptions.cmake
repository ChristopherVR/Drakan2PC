# Project-wide compiler flags for Drakan's own targets.
#
# Dependencies fetched with FetchContent (raylib, fmt, libdwarf, ...) and the generated game code do not get these
# flags. Link `drakan::options` (PRIVATE) into every library / executable we own:
#
#   target_link_libraries(my_lib PRIVATE drakan::options)
#
# Warnings are advisory: nothing here turns a warning into an error. Player builds (Build-Drakan.ps1) turn them off
# with DRAKAN_COMPILER_WARNINGS=OFF: they are for developers and would bury the build's progress in noise.
if(TARGET drakan_options)
    return()
endif()

add_library(drakan_options INTERFACE)
add_library(drakan::options ALIAS drakan_options)

option(DRAKAN_COMPILER_WARNINGS "Compile Drakan's own targets with warnings (/W4, -Wall -Wextra); OFF silences them" ON)

if(MSVC) # cl.exe and clang-cl
    if(DRAKAN_COMPILER_WARNINGS)
        target_compile_options(drakan_options INTERFACE /W4)
    else()
        target_compile_options(drakan_options INTERFACE /W0)
    endif()
    target_compile_options(drakan_options INTERFACE /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(drakan_options INTERFACE _CRT_SECURE_NO_WARNINGS)
elseif(DRAKAN_COMPILER_WARNINGS)
    target_compile_options(drakan_options INTERFACE -Wall -Wextra)
else()
    target_compile_options(drakan_options INTERFACE -w)
endif()

# Make a test / tool executable behave like the rest of the project and keep assert() active in Release builds.
function(drakan_enable_asserts target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
endfunction()
