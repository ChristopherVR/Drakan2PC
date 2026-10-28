# Mouse + keyboard + XInput input module (drakan::input). Windows only (XInput is loaded dynamically).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_input LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(input)

add_library(drakan_input STATIC
    src/Config.cpp
    src/FrameInput.cpp
    src/PadSynth.cpp
    src/Script.cpp
    src/HostInput.cpp
    src/MenuMouse.cpp
    src/DrakanInput.cpp
    src/BowAim.cpp
    src/QuickSlots.cpp)
add_library(drakan::input ALIAS drakan_input)
target_include_directories(drakan_input PUBLIC include)
if(WIN32)
    target_link_libraries(drakan_input PUBLIC user32)
endif()
target_link_libraries(drakan_input PRIVATE drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(menu_mouse_inventory_test tests/menu_mouse_inventory_tests.cpp)
    target_link_libraries(menu_mouse_inventory_test PRIVATE drakan_input drakan::options)
    add_executable(menu_mouse_map_test tests/menu_mouse_map_tests.cpp)
    target_link_libraries(menu_mouse_map_test PRIVATE drakan_input drakan::options)
    if(MSVC)
        target_compile_options(menu_mouse_map_test PRIVATE /WX)
    endif()
    add_executable(input_test tests/input_test.cpp)
    target_link_libraries(input_test PRIVATE drakan_input drakan::options)
    add_test(NAME input_test COMMAND input_test)
    add_executable(bow_aim_test tests/bow_aim_tests.cpp)
    target_link_libraries(bow_aim_test PRIVATE drakan_input drakan::options)
    add_test(NAME bow_aim_test COMMAND bow_aim_test)
    add_test(NAME menu_mouse_inventory_test COMMAND menu_mouse_inventory_test)
    add_test(NAME menu_mouse_map_test COMMAND menu_mouse_map_test)
    add_executable(inventory_views_test tests/inventory_views_tests.cpp)
    target_link_libraries(inventory_views_test PRIVATE drakan_input drakan::options)
    add_test(NAME inventory_views_test COMMAND inventory_views_test)
    add_executable(quick_slots_test tests/quick_slots_tests.cpp)
    target_link_libraries(quick_slots_test PRIVATE drakan_input drakan::options)
    add_test(NAME quick_slots_test COMMAND quick_slots_test)
    if(WIN32)
        add_executable(input_live tests/live_window.cpp)
        target_link_libraries(input_live PRIVATE drakan_input user32 gdi32 drakan::options)
    endif()
endif()
