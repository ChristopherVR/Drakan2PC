# Modern Tab screen core (drakan::gameui): model snapshot + guest reader, layout / navigation state machine producing
# draw commands as data, UI action queue with result verification. std only, platform free (no ImGui, no D3D).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_gameui LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(gameui)

add_library(drakan_gameui STATIC
    src/GuestMemory.cpp
    src/Dialog.cpp
    src/Model.cpp
    src/Reader.cpp
    src/Actions.cpp
    src/Layout.cpp
    src/Screen.cpp
    src/Paint.cpp
    src/StripScreen.cpp
    src/MapScreen.cpp
    src/PauseOptions.cpp
    src/PauseScreen.cpp
    src/SettingsSchema.cpp
    src/SettingsPages.cpp
    src/SaveSlots.cpp
    src/SaveLoadScreen.cpp
    src/TitleScreen.cpp
    src/UnifiedOptions.cpp)
add_library(drakan::gameui ALIAS drakan_gameui)
target_include_directories(drakan_gameui PUBLIC include)
target_link_libraries(drakan_gameui PRIVATE drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(gameui_dialog_test tests/gameui_dialog_test.cpp)
    target_link_libraries(gameui_dialog_test PRIVATE drakan_gameui drakan::options)
    add_test(NAME gameui_dialog_test COMMAND gameui_dialog_test)

    add_executable(gameui_test tests/gameui_test.cpp)
    target_link_libraries(gameui_test PRIVATE drakan_gameui drakan::options)
    add_test(NAME gameui_test COMMAND gameui_test)

    # Dual wielding: the hand rule shared with the runtime hooks (gameui/DualWield.h).
    add_executable(gameui_dual_wield_test tests/dual_wield_test.cpp)
    target_link_libraries(gameui_dual_wield_test PRIVATE drakan_gameui)
    add_test(NAME gameui_dual_wield_test COMMAND gameui_dual_wield_test)

    # StripScreen, MapScreen, PauseScreen.
    add_executable(gameui_screens_test tests/gameui_screens_test.cpp)
    target_link_libraries(gameui_screens_test PRIVATE drakan_gameui drakan::options)
    add_test(NAME gameui_screens_test COMMAND gameui_screens_test)

    # PauseScreen phase 2 / 3 pages (Options, Audio, Controller, confirm cards) and PauseOptionsState.
    add_executable(gameui_pause_test tests/gameui_pause_test.cpp)
    target_link_libraries(gameui_pause_test PRIVATE drakan_gameui drakan::options)
    add_test(NAME gameui_pause_test COMMAND gameui_pause_test)

    # SettingsPages: the modern PC Settings pages (navigation, cards, capture, the PauseScreen hook).
    add_executable(gameui_settings_test tests/gameui_settings_test.cpp)
    target_link_libraries(gameui_settings_test PRIVATE drakan_gameui drakan::options)
    add_test(NAME gameui_settings_test COMMAND gameui_settings_test)
    # SaveLoadScreen, TitleScreen, the slot reader and PauseScreen's embedded (rootPage) mode.
    add_executable(gameui_saveload_test tests/gameui_saveload_test.cpp)
    target_link_libraries(gameui_saveload_test PRIVATE drakan_gameui drakan::options)
    add_test(NAME gameui_saveload_test COMMAND gameui_saveload_test)

    # Offline preview: writes the screens' draw lists (synthetic data, several aspects) as JSON lines for
    # tests/render_drawlist.py. Not a test; run by hand: gameui_preview <outdir>.
    add_executable(gameui_preview tests/gameui_preview.cpp)
    target_link_libraries(gameui_preview PRIVATE drakan_gameui drakan::options)

    # Reads the local RAM dumps named in docs/journal/2026-10-24/tab-rework-data-model.md (build/, never committed);
    # exits 77 (skipped) when none is present. Paths can also be given as arguments.
    add_executable(gameui_ram_test tests/gameui_ram_test.cpp)
    target_link_libraries(gameui_ram_test PRIVATE drakan_gameui drakan::options)
    target_compile_definitions(gameui_ram_test PRIVATE "GAMEUI_REPO_ROOT=\"${DRAKAN_ROOT_DIR}\"")
    add_test(NAME gameui_ram_test COMMAND gameui_ram_test)
    set_tests_properties(gameui_ram_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
