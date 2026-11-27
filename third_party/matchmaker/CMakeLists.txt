# SimpleMatchmaker server, as vendored into OpenSWOS.
#
# Upstream built its own bundled ENET copy. We build against the ONE vendored
# ENET in third_party/enet instead (see OPENSWOS.md): the game's matchmaking
# client links that same copy, and two ENETs in one tree is how you get a
# wire-compatible-but-subtly-different pair.
#
# Only the SERVER is built here. The client half (ServerConnection/,
# P2PConnection/) is vendored as READABLE REFERENCE for src/mm_client.c and is
# deliberately not compiled: it is C++ and Windows-leaning (Sleep(), conio.h),
# while our client is C and has to build for every SDL target.
#
#   cmake -S third_party/matchmaker -B build/matchmaker -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/matchmaker -j
#
# scripts/run_matchmaker.sh does both and then runs it.
cmake_minimum_required(VERSION 3.10)
project(SwosMatchmaker CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../enet")

# ENET's build knobs are normally discovered by autotools/CMake feature checks.
# The set below is what its own CMakeLists probes for on a modern Linux/macOS;
# getting one wrong silently changes socket behaviour rather than failing to
# build, so they are listed explicitly instead of inherited.
set(ENET_SOURCES
    ${ENET_DIR}/callbacks.c
    ${ENET_DIR}/compress.c
    ${ENET_DIR}/host.c
    ${ENET_DIR}/list.c
    ${ENET_DIR}/packet.c
    ${ENET_DIR}/peer.c
    ${ENET_DIR}/protocol.c
)
if(WIN32)
    list(APPEND ENET_SOURCES ${ENET_DIR}/win32.c)
else()
    list(APPEND ENET_SOURCES ${ENET_DIR}/unix.c)
endif()

add_library(swos_enet STATIC ${ENET_SOURCES})
target_include_directories(swos_enet PUBLIC ${ENET_DIR}/include)
if(NOT WIN32)
    target_compile_definitions(swos_enet PUBLIC
        HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1
        HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1)
endif()

file(GLOB server_sources "${CMAKE_CURRENT_SOURCE_DIR}/Server/*.cpp")
file(GLOB common_sources "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp")

# OpenSWOS: the relay and the recorder (Server/Relay.cpp, Server/Recorder.cpp)
# share files with the game, so the two ends cannot drift: the network emulator
# (src/netem_core.c), the relay and recording protocols (include/relay_proto.h,
# include/record_proto.h), and the .SRP layout (src/srp_writer.c, with miniz from
# src/miniz_impl.c). Plain C, no SDL, nothing else from the game tree. See
# OPENSWOS.md.
set(SWOS_TREE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(swos-matchmaker ${server_sources} ${common_sources}
    "${SWOS_TREE}/src/netem_core.c"
    "${SWOS_TREE}/src/srp_writer.c"
    "${SWOS_TREE}/src/miniz_impl.c"
    # OpenSWOS-Indoor: the ranking desk's sealed messages (Server/RankDesk.cpp, include/rank_proto.h)
    "${SWOS_TREE}/src/rank_seal.c"
    "${SWOS_TREE}/third_party/monocypher/monocypher.c")
target_compile_definitions(swos-matchmaker PRIVATE RANK_SERVER=1)
find_package(Threads REQUIRED)
target_link_libraries(swos-matchmaker Threads::Threads)
target_include_directories(swos-matchmaker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Server/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include"
    "${SWOS_TREE}/include")
target_link_libraries(swos-matchmaker swos_enet)

# wss:// for the web build (Server/include/WebGate.h, docs/NETPLAY_SETUP.md §10).
# OpenSSL from the system (libssl-dev), so a TLS fix arrives with the distro's
# updates rather than waiting on us. Without it the server still builds and serves
# ws:// only, and says so if it is given a certificate.
find_package(OpenSSL)
if(OPENSSL_FOUND AND NOT WIN32)
    target_compile_definitions(swos-matchmaker PRIVATE SWOS_MM_TLS=1)
    target_link_libraries(swos-matchmaker OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "swos-matchmaker: wss:// with OpenSSL ${OPENSSL_VERSION}")
else()
    message(WARNING "swos-matchmaker: OpenSSL not found (apt install libssl-dev) -- "
                    "building without wss://")
endif()

if(WIN32)
    target_link_libraries(swos-matchmaker winmm ws2_32)
endif()

# The reference CLIENT, built only as a test peer (tools/mm_refpeer). Our client
# is src/mm_client.c and stays the only client the game ships; this exists so the
# two implementations can be pointed at each other -- see OPENSWOS.md, "The
# reference client as a test peer". It is not part of any game build.
add_executable(mm-refpeer
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/mm_refpeer/mm_refpeer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ServerConnection/ServerConnection.cpp"
    ${common_sources})
target_include_directories(mm-refpeer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ServerConnection"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/include")
target_link_libraries(mm-refpeer swos_enet)
if(WIN32)
    target_link_libraries(mm-refpeer winmm ws2_32)
endif()
