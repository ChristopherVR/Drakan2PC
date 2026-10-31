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

add_library(drakan_net STATIC
    src/Protocol.cpp
    src/LoopbackTransport.cpp
    src/MatchServer.cpp
    src/MatchClient.cpp)
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

    add_executable(net_coop_test tests/coop_test.cpp)
    target_link_libraries(net_coop_test PRIVATE drakan_net drakan::options)
    add_test(NAME net_coop_test COMMAND net_coop_test)

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
