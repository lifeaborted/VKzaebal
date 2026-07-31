cmake_minimum_required(VERSION 3.16)
project(VKAudioPlayer VERSION 0.1 LANGUAGES CXX)

# Задаем стандарт C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем автоматическую генерацию мета-кода Qt глобально
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Указываем путь к Qt (лучше передавать через переменные окружения, но оставляем для сборки)
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")

# Ищем нужные модули Qt
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network)

# Указываем путь к исходникам и заголовкам
set(PROJECT_SOURCES
        src/utils/logger/logger.h
        src/core/audio/AudioEngine/AudioEngine.h
        src/core/network/NetworkStreamer.h
        src/main.cpp
        src/core/audio/AudioEngine/AudioEngine.cpp
        src/core/audio/miniaudio/miniaudio_impl.cpp
        src/core/network/NetworkStreamer.cpp
        src/utils/logger/Logger.cpp
        src/utils/buffer/RingBuffer.h
        src/utils/buffer/RingBuffer.cpp
)

# Создаем исполняемый файл
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Привязываем путь к заголовкам конкретно к нашей цели (Target-based подход)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Линкуем модули Qt с приложением (добавлен Network, раз он запрашивался выше)
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Network
)
