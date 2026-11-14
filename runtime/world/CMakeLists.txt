# World model (drakan::world): typed read-only views of the guest's actor registry and a per-frame entity snapshot
# (handle, class, type, position, ...) with a double-buffered exchange and a JSON writer. std only, platform free (no
# ImGui, no Win32). Guest memory comes through gameui::IGuestMemory (the adapter lives in ps2xRuntime).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_world LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(world)
drakan_component_require(gameui)

add_library(drakan_world STATIC
    src/Reader.cpp
    src/Json.cpp
    src/Exchange.cpp
    src/Pose.cpp)
add_library(drakan::world ALIAS drakan_world)
target_include_directories(drakan_world PUBLIC include)
target_link_libraries(drakan_world PUBLIC drakan::gameui PRIVATE drakan::options)

if(DRAKAN_BUILD_RUNTIME_TESTS)
    add_executable(world_spatial_registration_test tests/spatial_registration_test.cpp)
    target_link_libraries(world_spatial_registration_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_spatial_registration_test COMMAND world_spatial_registration_test)

    add_executable(world_inventory_test tests/inventory_test.cpp)
    target_link_libraries(world_inventory_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_inventory_test COMMAND world_inventory_test)

    # Wind model of the living-foliage option (header only): determinism, bounds, travelling gusts, push.
    add_executable(world_foliage_test tests/foliage_test.cpp)
    target_link_libraries(world_foliage_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_foliage_test COMMAND world_foliage_test)

    # Model detail options (header only): LOD distance factor, look-changing LODs, draw distance factor.
    add_executable(world_model_detail_test tests/model_detail_test.cpp)
    target_link_libraries(world_model_detail_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_model_detail_test COMMAND world_model_detail_test)

    # Ground texture distance option (header only): factor clamp, the view scale the chunk draw sees, splat weights.
    add_executable(world_terrain_distance_test tests/terrain_distance_test.cpp)
    target_link_libraries(world_terrain_distance_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_terrain_distance_test COMMAND world_terrain_distance_test)

    # View distance option (header only): factor clamp, the fog setter's fields at a factor.
    add_executable(world_view_distance_test tests/view_distance_test.cpp)
    target_link_libraries(world_view_distance_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_view_distance_test COMMAND world_view_distance_test)

    # Engine budget rules (header only): shadow distance factor, model / animation cache budgets.
    add_executable(world_engine_limits_test tests/engine_limits_test.cpp)
    target_link_libraries(world_engine_limits_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_engine_limits_test COMMAND world_engine_limits_test)

    # Point lights per model (header only): the builder's accept rule, free slots, folding into four slots.
    add_executable(world_model_lights_test tests/model_lights_test.cpp)
    target_link_libraries(world_model_lights_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_model_lights_test COMMAND world_model_lights_test)

    # Extended guest heap (header only): best-fit allocator over the guest RAM above 32 MB, committed-top growth and
    # give-back, routing rules (terrain builder, overflow eligibility), churn.
    add_executable(world_extended_heap_test tests/extended_heap_test.cpp)
    target_link_libraries(world_extended_heap_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_extended_heap_test COMMAND world_extended_heap_test)
    add_executable(world_terrain_chunks_test tests/terrain_chunks_test.cpp)
    target_link_libraries(world_terrain_chunks_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_terrain_chunks_test COMMAND world_terrain_chunks_test)

    # Near-plane clip of the engine's projected polygons (header only): bit-identical front polygons, clipped straddlers.
    add_executable(world_polyclip_test tests/polyclip_test.cpp)
    target_link_libraries(world_polyclip_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_polyclip_test COMMAND world_polyclip_test)

    # DRAKAN_PLAYTEST_WATCH helpers (header only): setting parser, filter, line text.
    add_executable(world_watch_test tests/watch_test.cpp)
    target_link_libraries(world_watch_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_watch_test COMMAND world_watch_test)

    # Pose model: synthetic memory + interpolation (the bridge to the real net::PlayerState is tested in runtime/net).
    add_executable(world_pose_test tests/pose_test.cpp)
    target_link_libraries(world_pose_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_pose_test COMMAND world_pose_test)

    # Synthetic memory images only (built in the test); no game bytes are read or committed.
    add_executable(world_test tests/world_test.cpp)
    target_link_libraries(world_test PRIVATE drakan_world drakan::options)
    add_test(NAME world_test COMMAND world_test)

    # Local RAM dumps (build/, never committed); exits 77 (skipped) when none is present.
    add_executable(world_ram_test tests/world_ram_test.cpp)
    target_link_libraries(world_ram_test PRIVATE drakan_world drakan::options)
    target_compile_definitions(world_ram_test PRIVATE "WORLD_REPO_ROOT=\"${DRAKAN_ROOT_DIR}\"")
    add_test(NAME world_ram_test COMMAND world_ram_test)
    set_tests_properties(world_ram_test PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(world_pose_ram_test tests/pose_ram_test.cpp)
    target_link_libraries(world_pose_ram_test PRIVATE drakan_world drakan::options)
    target_compile_definitions(world_pose_ram_test PRIVATE "WORLD_REPO_ROOT=\"${DRAKAN_ROOT_DIR}\"")
    add_test(NAME world_pose_ram_test COMMAND world_pose_ram_test)
    set_tests_properties(world_pose_ram_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
