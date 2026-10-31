# Multiplayer layer (drakan::net): transport seam, protocol v1, authoritative MatchServer, MatchClient.
#   drakan_net      std-only public headers; loopback transport everywhere, UDP transport where raw sockets exist
#   drakan_server   headless dedicated server (no game, no ISO)
# See docs/reference/netcode.md.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) # standalone configure
    cmake_minimum_required(VERSION 3.21)
    project(drakan_net LANGUAGES CXX)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DrakanComponents.cmake")
drakan_component_begin(net)

# The multiplayer maps (configs/mp_maps.ini) are compiled in as the defaults; the player's own mp_maps.ini and mod packs
# add to them at run time. MSVC limits one string literal to ~16 KB, so the text goes in as 8 KB raw-string pieces.
set(DRAKAN_MP_MAPS_INI "${CMAKE_CURRENT_LIST_DIR}/../../configs/mp_maps.ini")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DRAKAN_MP_MAPS_INI}")
file(READ "${DRAKAN_MP_MAPS_INI}" _mp_maps_text)
string(LENGTH "${_mp_maps_text}" _mp_maps_len)
set(DRAKAN_MP_MAPS_CHUNKS "")
set(_mp_pos 0)
while(_mp_pos LESS _mp_maps_len)
    string(SUBSTRING "${_mp_maps_text}" ${_mp_pos} 8000 _mp_piece)
    string(APPEND DRAKAN_MP_MAPS_CHUNKS "
    + std::string(R\"MPMAPS(${_mp_piece})MPMAPS\")")
    math(EXPR _mp_pos "${_mp_pos} + 8000")
endwhile()
configure_file("${CMAKE_CURRENT_LIST_DIR}/src/ArenaMapsBuiltin.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/ArenaMapsBuiltin.cpp" @ONLY)

add_library(drakan_net STATIC
    src/Protocol.cpp
    src/LoopbackTransport.cpp
    src/MatchServer.cpp
    src/MatchClient.cpp
    src/ArenaMaps.cpp
    src/GameModes.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/ArenaMapsBuiltin.cpp")
add_library(drakan::net ALIAS drakan_net)
target_include_directories(drakan_net PUBLIC include)
target_link_libraries(drakan_net PRIVATE drakan::options)

# Raw UDP does not exist in the browser; a WebSocket / WebRTC Transport joins here for the web build.
if(NOT EMSCRIPTEN)
    target_sources(drakan_net PRIVATE src/UdpTransport.cpp)
    target_compile_definitions(drakan_net PUBLIC DRAKAN_NET_HAVE_UDP=1)
    if(WIN32)
        target_link_libraries(drakan_net PUBLIC ws2_32)
    endif()

    add_executable(drakan_server server/main.cpp)
    target_link_libraries(drakan_server PRIVATE drakan_net drakan::options)
endif()

if(DRAKAN_BUILD_RUNTIME_TESTS)
    # world::toPlayerState / fromPlayerState against the real wire type (world is a test-only dependency here).
    drakan_component_require(world)
    add_executable(net_pose_bridge_test tests/pose_bridge_test.cpp)
    target_link_libraries(net_pose_bridge_test PRIVATE drakan_net drakan::world drakan::options)
    add_test(NAME net_pose_bridge_test COMMAND net_pose_bridge_test)

    add_executable(net_protocol_test tests/protocol_test.cpp)
    target_link_libraries(net_protocol_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_protocol_test COMMAND net_protocol_test)

    add_executable(net_match_test tests/match_test.cpp)
    target_link_libraries(net_match_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_match_test COMMAND net_match_test)

    add_executable(net_lobby_test tests/lobby_test.cpp)
    target_link_libraries(net_lobby_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_lobby_test COMMAND net_lobby_test)

    add_executable(net_coop_test tests/coop_test.cpp)
    target_link_libraries(net_coop_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_coop_test COMMAND net_coop_test)

    add_executable(net_arena_test tests/arena_test.cpp)
    target_link_libraries(net_arena_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_arena_test COMMAND net_arena_test)

    add_executable(net_modes_test tests/modes_test.cpp)
    target_link_libraries(net_modes_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_modes_test COMMAND net_modes_test)

    add_executable(net_loopback_e2e_test tests/loopback_e2e_test.cpp)
    target_link_libraries(net_loopback_e2e_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_loopback_e2e_test COMMAND net_loopback_e2e_test)

    if(NOT EMSCRIPTEN)
        add_executable(net_udp_smoke_test tests/udp_smoke_test.cpp)
        target_link_libraries(net_udp_smoke_test PRIVATE drakan_net drakan::options)
        add_test(NAME net_udp_smoke_test COMMAND net_udp_smoke_test)
        set_tests_properties(net_udp_smoke_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
endif()
