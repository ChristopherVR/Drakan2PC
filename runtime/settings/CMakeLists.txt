# Drakan settings overlay: Dear ImGui menu (display / audio / controls / gameplay) + comment preserving ini reader/writer.
#   drakan_settings_core   ini model, store, binding catalogue, apply    (std only, platform free)
#   drakan_settings_schema the settings schema + ModernSettings glue     (std + gameui types, platform free)
#   drakan_settings_ui     SettingsMenu, style, fonts, GameUiRenderer    (ImGui API only, backend agnostic)
#   drakan_settings_d3d11  SettingsOverlay on Win32 + the D3D11 renderer (Windows only)
#   drakan_settings_glfw   SettingsOverlay on GLFW (Linux; drakan_settings_glfw_overlay(), once GLFW exists)
#   drakan_settings_webgpu the overlay's WebGPU renderer on gsr::rhi     (every target with drakan::gs_rhi)
#   drakan_settings        interface target = all of the above for the runtime (alias drakan::settings)
# Dear ImGui itself is third_party/imgui (drakan::imgui, drakan::imgui_dx11).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_settings LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(settings)
drakan_component_require(imgui gameui)
if(WIN32)
    drakan_component_require(hle spu2)
endif()

add_library(drakan_settings_core STATIC src/IniDoc.cpp src/Store.cpp src/Bindings.cpp src/Apply.cpp)
target_include_directories(drakan_settings_core PUBLIC include)
target_link_libraries(drakan_settings_core PRIVATE drakan::options)

# The schema both settings UIs draw (the F1 overlay and gameui::SettingsPages) and the modern pages' apply glue.
add_library(drakan_settings_schema STATIC src/Schema.cpp src/ModernSettings.cpp)
target_include_directories(drakan_settings_schema PUBLIC include)
target_link_libraries(drakan_settings_schema PUBLIC drakan_settings_core drakan::gameui PRIVATE drakan::options)

add_library(drakan_settings_ui STATIC src/Menu.cpp src/Skin.cpp src/GameUiRenderer.cpp src/Hud.cpp)
# third_party/fonts: the vendored Gelasio faces (OFL), compressed into sources.
target_include_directories(drakan_settings_ui PUBLIC include PRIVATE src "${CMAKE_CURRENT_LIST_DIR}/../../third_party/fonts")
target_link_libraries(drakan_settings_ui PUBLIC drakan_settings_core drakan_settings_schema drakan::imgui drakan::gameui
    PRIVATE drakan::options)

add_library(drakan_settings INTERFACE)
add_library(drakan::settings ALIAS drakan_settings)
target_link_libraries(drakan_settings INTERFACE drakan_settings_ui)

if(WIN32)
    add_library(drakan_settings_d3d11 STATIC src/Overlay.cpp src/OverlayWin32.cpp src/OverlayD3D11.cpp)
    target_include_directories(drakan_settings_d3d11 PUBLIC include)
    target_link_libraries(drakan_settings_d3d11 PUBLIC drakan_settings_ui drakan::imgui_dx11
        d3d11 dxgi dwmapi comctl32 PRIVATE drakan::options)
    target_link_libraries(drakan_settings INTERFACE drakan_settings_d3d11)
endif()

# SettingsOverlay on a GLFW window. GLFW comes with the game's raylib (target `glfw`, an object library already linked
# into raylib), so the game build calls this after raylib is available; only its headers are used here.
function(drakan_settings_glfw_overlay)
    if(TARGET drakan_settings_glfw)
        return()
    endif()
    set(dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    add_library(drakan_settings_glfw STATIC "${dir}/src/Overlay.cpp" "${dir}/src/OverlayGlfw.cpp"
        "${dir}/../../third_party/imgui/backends/imgui_impl_glfw.cpp")
    target_include_directories(drakan_settings_glfw PUBLIC "${dir}/include"
        PRIVATE "${dir}/src" "${dir}/../../third_party/imgui" $<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(drakan_settings_glfw PUBLIC drakan_settings_ui PRIVATE drakan::options)
    set_source_files_properties("${dir}/../../third_party/imgui/backends/imgui_impl_glfw.cpp" PROPERTIES
        COMPILE_OPTIONS "-w")
endfunction()

# SettingsOverlay fed by the host's event stream (the browser).
if(EMSCRIPTEN)
    add_library(drakan_settings_events STATIC src/Overlay.cpp src/OverlayEvents.cpp)
    target_include_directories(drakan_settings_events PUBLIC include PRIVATE src)
    target_link_libraries(drakan_settings_events PUBLIC drakan_settings_ui PRIVATE drakan::options)
    target_link_libraries(drakan_settings INTERFACE drakan_settings_events)
endif()

if(TARGET drakan::gs_rhi)
    add_library(drakan_settings_webgpu STATIC src/OverlayWebGpu.cpp)
    target_include_directories(drakan_settings_webgpu PUBLIC include)
    target_link_libraries(drakan_settings_webgpu PUBLIC drakan::imgui PRIVATE drakan::gs_rhi drakan::options)
    target_compile_definitions(drakan_settings_webgpu PUBLIC DRAKAN_SETTINGS_WEBGPU=1)
    target_link_libraries(drakan_settings INTERFACE drakan_settings_webgpu)
endif()

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(settings_test tests/settings_test.cpp)
    target_link_libraries(settings_test PRIVATE drakan_settings_core drakan::options)

    add_executable(settings_menu_test tests/settings_menu_test.cpp)
    target_link_libraries(settings_menu_test PRIVATE drakan_settings_ui drakan::options)

    # The schema (every field shown, values round trip) and ModernSettings (the modern pages' apply path).
    add_executable(settings_schema_test tests/settings_schema_test.cpp)
    target_link_libraries(settings_schema_test PRIVATE drakan_settings_schema drakan::options)
    target_include_directories(settings_schema_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gameui/tests")

    # Offline preview of the modern PC Settings pages over the real schema (JSON lines for
    # runtime/gameui/tests/render_drawlist.py). Not a test; run by hand: settings_pages_preview <outdir>.
    add_executable(settings_pages_preview tests/settings_pages_preview.cpp)
    target_link_libraries(settings_pages_preview PRIVATE drakan_settings_schema drakan::options)
    target_include_directories(settings_pages_preview PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../gameui/tests")

    add_test(NAME settings_test COMMAND settings_test)
    add_test(NAME settings_schema_test COMMAND settings_schema_test)
    add_test(NAME settings_menu_test COMMAND settings_menu_test)

    if(TARGET drakan_settings_webgpu AND NOT EMSCRIPTEN)
        add_executable(overlay_webgpu_test tests/overlay_webgpu_test.cpp)
        target_link_libraries(overlay_webgpu_test PRIVATE drakan_settings_webgpu drakan::gs_rhi drakan::options)
        drakan_stage_webgpu(overlay_webgpu_test)
        add_test(NAME overlay_webgpu_test COMMAND overlay_webgpu_test)
        set_tests_properties(overlay_webgpu_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    endif()

    if(WIN32)
        add_executable(subtitle_render_test tests/subtitle_render_test.cpp)
        target_link_libraries(subtitle_render_test PRIVATE drakan_settings drakan::options)
        add_test(NAME subtitle_render_test COMMAND subtitle_render_test)

        # Host-side subtitle toggle contract (previously registered from the ps2xRuntime build, where it never ran).
        add_executable(drakan_subtitle_toggle_test
            "${DRAKAN_PS2RECOMP_DIR}/ps2xRuntime/tests/subtitle_toggle_test.cpp")
        target_compile_definitions(drakan_subtitle_toggle_test PRIVATE PS2X_HAVE_DRAKAN_SETTINGS=1)
        target_link_libraries(drakan_subtitle_toggle_test PRIVATE drakan_settings drakan::hle drakan::spu2 drakan::options)
        add_test(NAME drakan_subtitle_toggle_test COMMAND drakan_subtitle_toggle_test)

        add_executable(settings_demo tests/settings_demo.cpp)
        target_link_libraries(settings_demo PRIVATE drakan_settings d3d11 dxgi windowscodecs ole32 dbghelp drakan::options)
        # Standalone builds also drop the demo next to its source (tests/settings_demo.exe, git-ignored).
        if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
            add_custom_command(TARGET settings_demo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:settings_demo> ${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_demo.exe)
        endif()
    endif()
endif()
