cmake_minimum_required(VERSION 3.10)
project(AIAgent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# 1. Подключение зависимостей через vcpkg
# ------------------------------------------------------------------

find_package(GTest REQUIRED)
find_package(toml11 REQUIRED)
find_package(Td REQUIRED)

if(Td_FOUND)
    message(STATUS "Td found, libraries: Td::tdjson_static")
else()
    message(FATAL_ERROR "Td not found")
endif()

# ------------------------------------------------------------------
# 2. Поиск include-директории TDLib (если не задана)
# ------------------------------------------------------------------
if(NOT Td_INCLUDE_DIRS)
    # Пытаемся найти Client.h в стандартных местах
    find_path(Td_INCLUDE_DIRS td/telegram/Client.h
        PATHS 
            ${CMAKE_PREFIX_PATH}/../include
            ${CMAKE_PREFIX_PATH}/../../include
            ~/vcpkg/vcpkg/installed/x64-linux/include
            /usr/local/include
            /usr/include
        REQUIRED
    )
    message(STATUS "Found Td include dir: ${Td_INCLUDE_DIRS}")
else()
    message(STATUS "Td_INCLUDE_DIRS already set: ${Td_INCLUDE_DIRS}")
endif()

# ------------------------------------------------------------------
# 3. Общая директория для заголовков
# ------------------------------------------------------------------
include_directories(include)

# ------------------------------------------------------------------
# 4. Библиотеки проекта
# ------------------------------------------------------------------

# 4.1. Очередь событий
add_library(event_queue
    src/PriorityEventQueue.cpp
    src/MockEventQueue.cpp
    src/PriorityResolver.cpp
)
target_include_directories(event_queue PUBLIC include)

# 4.2. Telegram-клиент
add_library(telegram
    src/TelegramClient.cpp
    src/MockTelegramClient.cpp
)
target_include_directories(telegram PUBLIC include)
target_include_directories(telegram PRIVATE ${Td_INCLUDE_DIRS})
target_link_libraries(telegram Td::tdjson_static)

# 4.3. Основной цикл агента
add_library(core
    src/AgentMainLoop.cpp
    src/EchoMessageHandler.cpp
)
target_include_directories(core PUBLIC include)
target_link_libraries(core
    event_queue
    telegram
)

# 4.4. Конфигурация
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

target_link_libraries(ai_agent
    core
    event_queue
    telegram
    config
    Td::tdjson_static
    pthread
)
target_include_directories(ai_agent PRIVATE include)
# ai_agent не использует TDLib напрямую, поэтому include не добавляем

# ------------------------------------------------------------------
# 6. Тесты
# ------------------------------------------------------------------
add_executable(event_tests
    tests/EventQueueTests.cpp
    tests/TelegramClientTests.cpp
    tests/AgentMainLoopTests.cpp
)

target_include_directories(event_tests PRIVATE include src)
target_include_directories(event_tests PRIVATE ${Td_INCLUDE_DIRS})  # на всякий случай
target_link_libraries(event_tests
    event_queue
    telegram
    core
    config
    Td::tdjson_static
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
    GTest::gmock_main
    pthread
)

# ------------------------------------------------------------------
# 7. CTest
# ------------------------------------------------------------------
enable_testing()
add_test(NAME event_tests COMMAND event_tests)