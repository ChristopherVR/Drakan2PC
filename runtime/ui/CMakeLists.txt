# Keyboard/mouse prompt glyphs, bindings text and the resource override folder (drakan::ui).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_ui LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(ui)
drakan_component_require(hle imgui)

add_library(drakan_ui STATIC src/Txd.cpp src/Bindings.cpp src/Draw.cpp src/Prompts.cpp src/TextRules.cpp src/FontAtlas.cpp)
add_library(drakan::ui ALIAS drakan_ui)
target_include_directories(drakan_ui PUBLIC include)
# FontAtlas.cpp reuses Dear ImGui's bundled stb_truetype (imstb_truetype.h).
target_link_libraries(drakan_ui PUBLIC drakan::hle PRIVATE drakan::imgui drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(font_atlas_test tests/font_atlas_test.cpp)
    target_link_libraries(font_atlas_test PRIVATE drakan_ui drakan::options)
    add_executable(ui_test tests/ui_test.cpp)
    target_link_libraries(ui_test PRIVATE drakan_ui drakan::options)
    add_executable(ui_preview tests/ui_preview.cpp)
    target_link_libraries(ui_preview PRIVATE drakan_ui drakan::options)
    # With no arguments both tests run their synthetic cases only; disc/font cases need <iso> [font] arguments.
    add_test(NAME font_atlas_test COMMAND font_atlas_test)
    add_test(NAME ui_test COMMAND ui_test)
endif()
