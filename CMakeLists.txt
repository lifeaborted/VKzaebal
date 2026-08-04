cmake_minimum_required(VERSION 3.16)
project(VKAudioPlayer VERSION 0.1 LANGUAGES CXX)

# Задаем стандарт C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем автоматическую генерацию мета-кода Qt глобально
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Указываем путь к Qt
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")

# Ищем базовые модули Qt (Gui нужен для QDesktopServices)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql)

# Указываем путь к исходникам и заголовкам
set(PROJECT_SOURCES
        src/main.cpp
        src/utils/logger/logger.h
        src/utils/logger/Logger.cpp
        src/utils/buffer/RingBuffer.h
        src/utils/buffer/RingBuffer.cpp
        src/core/playlist/PlaylistManager.cpp
        src/core/playlist/PlaylistManager.h
        src/core/vk/Track.h
        src/core/vk/VkApiClient/VkApiClient.cpp
        src/core/vk/VkApiClient/VkApiClient.h
        src/core/audio/AudioEngine/AudioEngine.cpp
        src/core/audio/AudioEngine/AudioEngine.h
        src/core/vk/VkAuthManager/VkAuthManager.cpp
        src/core/vk/VkAuthManager/VkAuthManager.h
        src/utils/DatabaseManager/DatabaseManager.cpp
        src/utils/DatabaseManager/DatabaseManager.h
        src/utils/ConsoleController/ConsoleController.cpp
        src/utils/ConsoleController/ConsoleController.h
        src/utils/TrackDownloader/TrackDownloader.cpp
        src/utils/TrackDownloader/TrackDownloader.h
        src/core/network/NetworkStreamer.cpp
        src/core/network/NetworkStreamer.h
)

# Создаем исполняемый файл
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Привязываем путь к заголовкам
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass
)

# Линкуем модули Qt с приложением
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::Sql
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/bass.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/basshls.lib
)