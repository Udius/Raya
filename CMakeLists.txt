cmake_minimum_required(VERSION 3.10)
project(EventQueue)

set(CMAKE_CXX_STANDARD 17)

# Подключаем GTest (предварительно установите sudo apt install libgtest-dev или через find_package)
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# Основная библиотека
add_library(event_queue
    src/PriorityEventQueue.cpp
    src/MockEventQueue.cpp
    src/PriorityResolver.cpp
    src/TelegramClient.cpp
    src/MockTelegramClient.cpp
)

# Добавляем корневые папки для включения заголовков
target_include_directories(event_queue PUBLIC include)
target_include_directories(event_queue PRIVATE src)   # для внутреннего использования MockTelegramClient.h

# Тесты
add_executable(event_tests tests/EventQueueTests.cpp tests/TelegramClientTests.cpp)
target_include_directories(event_tests PRIVATE include src)
target_link_libraries(event_tests event_queue GTest::gtest GTest::gtest_main pthread)