cmake_minimum_required(VERSION 3.20)
project(polymarket_arb VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
FetchContent_MakeAvailable(nlohmann_json ixwebsocket)

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
  src/ws/market_ws.cpp
  src/config.cpp
  src/benchmark.cpp
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
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
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
