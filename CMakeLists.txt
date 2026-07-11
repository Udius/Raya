cmake_minimum_required(VERSION 3.10)
project(AIAgent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------
# 1. Подключение зависимостей через vcpkg
# ------------------------------------------------------------------

# GTest (для тестов)
find_package(GTest REQUIRED)

# toml11 через vcpkg
find_package(toml11 REQUIRED)

# ------------------------------------------------------------------
# 2. Общая директория для заголовков
# ------------------------------------------------------------------
include_directories(include)

# ------------------------------------------------------------------
# 3. Библиотеки проекта
# ------------------------------------------------------------------

# 3.1. Очередь событий (EventQueue + PriorityResolver + моки)
add_library(event_queue
    src/PriorityEventQueue.cpp
    src/MockEventQueue.cpp
    src/PriorityResolver.cpp
)
target_include_directories(event_queue PUBLIC include)

# 3.2. Telegram-клиент (реальный и мок)
add_library(telegram
    src/TelegramClient.cpp
    src/MockTelegramClient.cpp
)
target_include_directories(telegram PUBLIC include)

# 3.3. Основной цикл агента и обработчики
add_library(core
    src/AgentMainLoop.cpp
    src/EchoMessageHandler.cpp
)
target_include_directories(core PUBLIC include)
target_link_libraries(core
    event_queue
    telegram
)

# 3.4. Конфигурация (загрузка TOML)
add_library(config
    src/Config.cpp
)
target_include_directories(config PUBLIC include)
target_link_libraries(config
    toml11::toml11   # vcpkg предоставляет этот таргет
)

# ------------------------------------------------------------------
# 4. Исполняемый файл приложения
# ------------------------------------------------------------------
add_executable(ai_agent src/main.cpp)

target_link_libraries(ai_agent
    core
    event_queue
    telegram
    config
    pthread
)
target_include_directories(ai_agent PRIVATE include)

# ------------------------------------------------------------------
# 5. Тесты
# ------------------------------------------------------------------
add_executable(event_tests
    tests/EventQueueTests.cpp
    tests/TelegramClientTests.cpp
    tests/AgentMainLoopTests.cpp   # если есть
)

target_include_directories(event_tests PRIVATE include src)
target_link_libraries(event_tests
    event_queue
    telegram
    core
    config
    gtest
    gtest_main
    gmock
    gmock_main 
    pthread
)

# ------------------------------------------------------------------
# 6. Опционально: настройка CTest
# ------------------------------------------------------------------
enable_testing()
add_test(NAME event_tests COMMAND event_tests)