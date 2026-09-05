cmake_minimum_required(VERSION 3.20)
project(polymarket_arb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PM_NATIVE_OPTIMIZE "Enable -march=native for release builds" ON)
option(PM_ENABLE_LTO "Enable link-time optimization for release builds" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
  )
  if(PM_NATIVE_OPTIMIZE)
    add_compile_options($<$<CONFIG:Release>:-march=native>)
  endif()
  if(PM_ENABLE_LTO)
    add_compile_options($<$<CONFIG:Release>:-flto>)
    add_link_options($<$<CONFIG:Release>:-flto>)
  endif()
endif()

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG v3.9.4
  GIT_SHALLOW TRUE
)
set(USE_TLS ON CACHE BOOL "Enable TLS in IXWebSocket" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "Use OpenSSL for IXWebSocket TLS" FORCE)
FetchContent_Declare(
  secp256k1
  GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
  GIT_TAG v0.5.1
  GIT_SHALLOW TRUE
)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json ixwebsocket simdjson secp256k1)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_library(pm_core
  src/http_client.cpp
  src/gamma_client.cpp
  src/clob_client.cpp
  src/order_book.cpp
  src/fee_model.cpp
  src/arb_engine.cpp
  src/auth/l2_auth.cpp
  src/auth/keccak256.cpp
  src/auth/eip712_sign.cpp
  src/ws/market_ws.cpp
  src/ws/book_updates.cpp
  src/ws/simdjson_book_parser.cpp
  src/fast_decimal.cpp
  src/trade_fire.cpp
  src/config.cpp
  src/benchmark.cpp
  src/game_state.cpp
  src/order_chamber.cpp
  src/market_tracker.cpp
)

target_include_directories(pm_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pm_core
  PUBLIC
    nlohmann_json::nlohmann_json
    ixwebsocket::ixwebsocket
    simdjson::simdjson
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    secp256k1
    Threads::Threads
)

add_executable(polymarket-arb src/main.cpp)
target_link_libraries(polymarket-arb PRIVATE pm_core)

enable_testing()
add_executable(pm_test_arb tests/test_arb.cpp)
add_executable(pm_test_fee tests/test_fee.cpp)
target_link_libraries(pm_test_arb PRIVATE pm_core)
target_link_libraries(pm_test_fee PRIVATE pm_core)
add_test(NAME pm_test_arb COMMAND pm_test_arb)
add_test(NAME pm_test_fee COMMAND pm_test_fee)
add_executable(pm_test_game_state tests/test_game_state.cpp)
target_link_libraries(pm_test_game_state PRIVATE pm_core)
add_test(NAME pm_test_game_state COMMAND pm_test_game_state)
add_executable(pm_test_order_book tests/test_order_book.cpp)
target_link_libraries(pm_test_order_book PRIVATE pm_core)
add_test(NAME pm_test_order_book COMMAND pm_test_order_book)
add_executable(pm_test_order_chamber tests/test_order_chamber.cpp)
target_link_libraries(pm_test_order_chamber PRIVATE pm_core)
add_test(NAME pm_test_order_chamber COMMAND pm_test_order_chamber)
add_executable(pm_test_spsc_ring tests/test_spsc_ring.cpp)
target_link_libraries(pm_test_spsc_ring PRIVATE pm_core)
add_test(NAME pm_test_spsc_ring COMMAND pm_test_spsc_ring)
