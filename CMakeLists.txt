cmake_minimum_required(VERSION 3.25)

# Optional Valve GameNetworkingSockets transport (D-012). Needs vcpkg; configure with
#   -DMORDOR_WITH_GNS=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# The default build stays dependency-free.
option(MORDOR_WITH_GNS "Build the GameNetworkingSockets transport and its tests" OFF)
if(NOT MORDOR_WITH_GNS)
  set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "" FORCE)
else()
  # The pinned manifest is the GNS build's dependency source: restore it in a directory first configured without GNS,
  # where the OFF above stayed in the cache (Codex, PR #24).
  set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "" FORCE)
endif()
project(ProjectMordor VERSION 0.1.0 LANGUAGES CXX)

# Phase 1 builds only the standalone networking prototype. Nothing here touches the game.
if(NOT WIN32)
  message(FATAL_ERROR "The prototype targets Windows (Winsock2).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc)
  add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()

add_library(mordor_common STATIC src/common/log.cpp src/common/precise_sleep.cpp)
target_include_directories(mordor_common PUBLIC src)

add_library(mordor_network STATIC src/network/protocol.cpp src/network/udp_socket.cpp src/network/impairment.cpp
  src/network/transport_factory.cpp src/network/udp_transport.cpp)
target_link_libraries(mordor_network PUBLIC mordor_common ws2_32)

add_library(mordor_simulation STATIC src/simulation/world.cpp)
target_link_libraries(mordor_simulation PUBLIC mordor_network)

add_library(mordor_host_lib STATIC src/host/host.cpp)
target_link_libraries(mordor_host_lib PUBLIC mordor_simulation)

add_library(mordor_client_lib STATIC src/client/client.cpp)
target_link_libraries(mordor_client_lib PUBLIC mordor_simulation)

add_executable(mordor-host src/host/main.cpp)
target_link_libraries(mordor-host PRIVATE mordor_host_lib)

add_executable(mordor-client src/client/main.cpp)
target_link_libraries(mordor-client PRIVATE mordor_client_lib)

enable_testing()
foreach(suite unit integration network transport)
  add_executable(mordor_${suite}_tests tests/${suite}/${suite}_tests.cpp tests/test_main.cpp)
  target_include_directories(mordor_${suite}_tests PRIVATE tests)
  target_link_libraries(mordor_${suite}_tests PRIVATE mordor_host_lib mordor_client_lib)
  add_test(NAME ${suite} COMMAND mordor_${suite}_tests --json ${CMAKE_BINARY_DIR}/test-results/${suite}.json --suite ${suite})
  # FB-13-03: pinned, so a MORDOR_TRANSPORT left in the caller's environment cannot change what a plain suite runs.
  set_tests_properties(${suite} PROPERTIES TIMEOUT 300 ENVIRONMENT "MORDOR_TRANSPORT=udp")
endforeach()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-results)

if(MORDOR_WITH_GNS)
  find_package(GameNetworkingSockets CONFIG REQUIRED)
  target_sources(mordor_network PRIVATE src/network/gns_transport.cpp)
  target_compile_definitions(mordor_network PUBLIC MORDOR_HAVE_GNS)
  target_link_libraries(mordor_network PUBLIC GameNetworkingSockets::shared)
  # D-013 option A research prototype (review #6 FB-6-01/02): DTLS 1.2 + RFC 7250 over Transport::Unreliable.
  find_package(OpenSSL REQUIRED)
  target_sources(mordor_network PRIVATE src/network/dtls_session.cpp src/network/secure_transport.cpp src/network/secure_identity.cpp)
  target_compile_definitions(mordor_network PUBLIC MORDOR_HAVE_DTLS)
  target_link_libraries(mordor_network PUBLIC OpenSSL::SSL OpenSSL::Crypto crypt32 advapi32)
  # Identity keys and invitations for secure mode (review #8 FB-8-03).
  add_executable(mordor-keygen src/tools/keygen_main.cpp)
  target_link_libraries(mordor-keygen PRIVATE mordor_network)
  add_executable(mordor_gns_tests tests/gns/gns_tests.cpp tests/test_main.cpp)
  target_include_directories(mordor_gns_tests PRIVATE tests)
  target_link_libraries(mordor_gns_tests PRIVATE mordor_host_lib mordor_client_lib GameNetworkingSockets::shared)
  add_test(NAME gns COMMAND mordor_gns_tests --json ${CMAKE_BINARY_DIR}/test-results/gns.json --suite gns)
  set_tests_properties(gns PROPERTIES TIMEOUT 300 ENVIRONMENT "MORDOR_TRANSPORT=udp")  # label only: it drives GNS itself
  # D-013 proof test (review #5 FB-5-05, approved by the owner): DTLS 1.2 + RFC 7250 raw public keys.
  add_executable(mordor_dtls_tests tests/dtls/dtls_proof_tests.cpp tests/dtls/dtls_transport_tests.cpp tests/dtls/secure_transport_tests.cpp tests/dtls/secure_identity_tests.cpp tests/test_main.cpp)
  target_include_directories(mordor_dtls_tests PRIVATE tests)
  target_link_libraries(mordor_dtls_tests PRIVATE mordor_host_lib mordor_client_lib OpenSSL::SSL OpenSSL::Crypto)
  add_test(NAME dtls COMMAND mordor_dtls_tests --json ${CMAKE_BINARY_DIR}/test-results/dtls.json --suite dtls)
  set_tests_properties(dtls PROPERTIES TIMEOUT 300 ENVIRONMENT "MORDOR_TRANSPORT=udp")  # label only: loops over both backends
  # KI-27 standalone reproducer (GNS public API only; tools/ki27-repro also builds on its own for the upstream report).
  add_executable(gns_listen_close_repro tools/ki27-repro/main.cpp)
  target_link_libraries(gns_listen_close_repro PRIVATE GameNetworkingSockets::shared)
  # KI-35 / EXP-036: GNS establishment timing probe (measurement tool; tools/ki35/run_probe.py records it).
  add_executable(ki35_probe tools/ki35/probe.cpp)
  target_include_directories(ki35_probe PRIVATE src)
  target_link_libraries(ki35_probe PRIVATE mordor_network)
  # KI-35 / EXP-037: per-connection DTLS and session phases of a secure join (tools/ki35/run_secure_probe.py).
  add_executable(ki35_secure_probe tools/ki35/secure_probe.cpp)
  target_include_directories(ki35_secure_probe PRIVATE src)
  target_link_libraries(ki35_secure_probe PRIVATE mordor_host_lib mordor_client_lib OpenSSL::SSL OpenSSL::Crypto)
  # The same host/client suites, run on the GNS adapter (results recorded per transport).
  foreach(suite integration network)
    add_test(NAME ${suite}-gns COMMAND mordor_${suite}_tests --json ${CMAKE_BINARY_DIR}/test-results/${suite}-gns.json --suite ${suite}-gns)
    set_tests_properties(${suite}-gns PROPERTIES TIMEOUT 1200 ENVIRONMENT "MORDOR_TRANSPORT=gns")
  endforeach()
endif()

# Codex review of the combined diff (P2): every executable runs with a UTF-8 ANSI code page (see the manifest), so
# non-ASCII paths on the command line (keys, logs, %TEMP%) arrive intact whatever the system code page is.
foreach(exe mordor-host mordor-client mordor_unit_tests mordor_integration_tests mordor_network_tests mordor_transport_tests
            mordor-keygen mordor_gns_tests mordor_dtls_tests gns_listen_close_repro ki35_probe ki35_secure_probe)
  if(TARGET ${exe})
    target_sources(${exe} PRIVATE ${CMAKE_SOURCE_DIR}/cmake/utf8-codepage.manifest)
  endif()
endforeach()
