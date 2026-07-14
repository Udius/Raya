cmake_minimum_required(VERSION 3.10)
project(AIAgent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# 1. Путь к локальной сборке TDLib (статическая линковка)
# ------------------------------------------------------------------
set(TDLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tdlib")
set(TDLIB_INCLUDE_DIR "${TDLIB_ROOT}/include")

set(TDLIB_STATIC_LIBS
    -Wl,--start-group
    ${TDLIB_ROOT}/lib/libtde2e.a
    ${TDLIB_ROOT}/lib/libtdcore.a
    ${TDLIB_ROOT}/lib/libtdclient.a
    ${TDLIB_ROOT}/lib/libtdnet.a
    ${TDLIB_ROOT}/lib/libtddb.a
    ${TDLIB_ROOT}/lib/libtdsqlite.a
    ${TDLIB_ROOT}/lib/libtdutils.a
    ${TDLIB_ROOT}/lib/libtdactor.a
    ${TDLIB_ROOT}/lib/libtdmtproto.a
    ${TDLIB_ROOT}/lib/libtdapi.a
    ${TDLIB_ROOT}/lib/libtdjson_static.a
    -Wl,--end-group
)

add_definitions(-DTD_STATIC)

# ------------------------------------------------------------------
# 2. Поиск системных зависимостей
# ------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GTest REQUIRED)
find_package(toml11 REQUIRED)
find_package(CURL REQUIRED)

find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES json.hpp PATH_SUFFIXES nlohmann)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp not found. Please install nlohmann-json3-dev")
endif()
include_directories(${NLOHMANN_JSON_INCLUDE_DIR})

# ------------------------------------------------------------------
# 3. Общая директория для заголовков проекта
# ------------------------------------------------------------------
include_directories(include)

# ------------------------------------------------------------------
# 4. Библиотеки проекта
# ------------------------------------------------------------------

add_library(event_queue
    src/PriorityEventQueue.cpp
    src/MockEventQueue.cpp
    src/PriorityResolver.cpp
)
target_include_directories(event_queue PUBLIC include)

add_library(telegram
    src/TelegramClient.cpp
    src/MockTelegramClient.cpp
)
target_include_directories(telegram PUBLIC include)
target_include_directories(telegram PRIVATE ${TDLIB_INCLUDE_DIR})
target_link_libraries(telegram
    ${TDLIB_STATIC_LIBS}
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    dl
    pthread
)

add_library(core
    src/AgentMainLoop.cpp
    src/EchoMessageHandler.cpp
    src/LLMMessageHandler.cpp
    src/OpenAIChatImpl.cpp
    src/ToolRegistry.cpp
)
target_include_directories(core PUBLIC include)
target_link_libraries(core
    event_queue
    telegram
    config
    ${CURL_LIBRARIES}
)

add_library(config
    src/Config.cpp
)
target_include_directories(config PUBLIC include)
target_link_libraries(config
    toml11::toml11
)

# ------------------------------------------------------------------
# 5. Исполняемый файл приложения
# ------------------------------------------------------------------
add_executable(ai_agent src/main.cpp)

target_include_directories(ai_agent PRIVATE include src ${TDLIB_INCLUDE_DIR})

target_link_libraries(ai_agent
    core
    event_queue
    telegram
    config
    ${TDLIB_STATIC_LIBS}
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${CURL_LIBRARIES}
    dl
    pthread
)

target_include_directories(core PRIVATE ${CURL_INCLUDE_DIRS})
target_include_directories(ai_agent PRIVATE ${CURL_INCLUDE_DIRS})

# ------------------------------------------------------------------
# 6. Тесты
# ------------------------------------------------------------------
add_executable(event_tests
    tests/EventQueueTests.cpp
    tests/TelegramClientTests.cpp
    tests/AgentMainLoopTests.cpp
)

target_include_directories(event_tests PRIVATE include src ${TDLIB_INCLUDE_DIR})

target_link_libraries(event_tests
    event_queue
    telegram
    core
    config
    ${TDLIB_STATIC_LIBS}
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    dl
    pthread
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
)

# ------------------------------------------------------------------
# 7. CTest
# ------------------------------------------------------------------
enable_testing()
add_test(NAME event_tests COMMAND event_tests)