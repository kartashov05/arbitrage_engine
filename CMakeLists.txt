cmake_minimum_required(VERSION 3.22)

project(arbitrage_engine VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

find_package(OpenSSL REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)

if (TARGET protobuf::libprotobuf)
    set(PROTOBUF_LIB_TARGET protobuf::libprotobuf)
elseif (TARGET Protobuf::libprotobuf)
    set(PROTOBUF_LIB_TARGET Protobuf::libprotobuf)
else()
    message(FATAL_ERROR "Could not find protobuf lib target")
endif()

find_package(Boost CONFIG REQUIRED)

if (TARGET Boost::headers)
    set(BOOST_HEADERS_TARGET Boost::headers)
elseif (TARGET Boost::boost)
    set(BOOST_HEADERS_TARGET Boost::boost)
else()
    message(FATAL_ERROR "Could not find Boost headers target")
endif()

if (TARGET yaml-cpp::yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
    set(YAML_CPP_TARGET yaml-cpp)
endif()

set(MEXC_PROTO_FILES
    ${PROJECT_SOURCE_DIR}/proto/mexc/PushDataV3ApiWrapper.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicAggreDepthsV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicAggreDealsV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicAggreBookTickerV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicDealsV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicIncreaseDepthsV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicLimitDepthsV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicBookTickerV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicBookTickerBatchV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicIncreaseDepthsBatchV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicMiniTickerV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicMiniTickersV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PublicSpotKlineV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PrivateOrdersV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PrivateDealsV3Api.proto
    ${PROJECT_SOURCE_DIR}/proto/mexc/PrivateAccountV3Api.proto
)

set(ABSL_PROTOBUF_DEPS
    absl::base
    absl::strings
    absl::log
    absl::check
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_conditions
    absl::log_internal_nullguard
    absl::raw_logging_internal
    absl::throw_delegate
)

protobuf_generate_cpp(
    MEXC_PROTO_SRCS
    MEXC_PROTO_HDRS
    ${MEXC_PROTO_FILES}
)

add_library(mexc_proto
    ${MEXC_PROTO_SRCS}
    ${MEXC_PROTO_HDRS}
)

target_include_directories(mexc_proto
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(mexc_proto
    PUBLIC
        ${PROTOBUF_LIB_TARGET}
        ${ABSL_PROTOBUF_DEPS}
)

add_library(arbitrage_core
    src/config/Config.cpp
    src/core/OrderBook.cpp
    src/engine/ArbitrageEngine.cpp
    src/exchanges/BinanceDepthParser.cpp
    src/exchanges/BinanceLocalBookManager.cpp
    src/exchanges/BinanceSnapshotParser.cpp
    src/exchanges/MexcDepthParser.cpp
    src/exchanges/MexcSnapshotParser.cpp
    src/exchanges/MexcLocalBookManager.cpp
    src/market/MarketDataStore.cpp
    src/net/HttpsClient.cpp
    src/net/WebSocketClient.cpp
    src/planner/SubscriptionPlanner.cpp
)

target_include_directories(arbitrage_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(arbitrage_core
    PUBLIC
        BOOST_ERROR_CODE_HEADER_ONLY
        BOOST_SYSTEM_NO_LIB
)

target_link_libraries(arbitrage_core
    PUBLIC
        mexc_proto
        ${BOOST_HEADERS_TARGET}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ${YAML_CPP_TARGET}
        spdlog::spdlog
        nlohmann_json::nlohmann_json
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arbitrage_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(arbitrage_engine
    src/main.cpp
)

target_link_libraries(arbitrage_engine
    PRIVATE
        arbitrage_core
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(arbitrage_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)

if (BUILD_TESTS)
    find_package(GTest REQUIRED)

    add_executable(arbitrage_tests
        tests/test_subscription_planner.cpp
        tests/test_order_book.cpp
        tests/test_arbitrage_engine.cpp
        tests/test_binance_depth_parser.cpp
        tests/test_binance_snapshot_parser.cpp
        tests/test_binance_local_book_manager.cpp
        tests/test_market_data_store.cpp
        tests/test_mexc_snapshot_parser.cpp
        tests/test_mexc_depth_parser.cpp
        tests/test_mexc_local_book_manager.cpp
    )

    target_link_libraries(arbitrage_tests
        PRIVATE
            arbitrage_core
            GTest::gtest_main
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(arbitrage_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    include(GoogleTest)
    gtest_discover_tests(arbitrage_tests)
endif()