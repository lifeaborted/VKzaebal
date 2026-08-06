cmake_minimum_required(VERSION 3.16)
project(VKAudioPlayer VERSION 0.1 LANGUAGES C CXX)

# Задаем стандарт C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем автоматическую генерацию мета-кода Qt глобально
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")

# Ищем базовые модули Qt (Gui нужен для QDesktopServices)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql)

# Указываем путь к исходникам и заголовкам
set(PROJECT_SOURCES
        src/main.cpp

        # --- Models ---
        src/models/Track.h

        # --- Core ---
        # Audio
        src/core/audio/IAudioEngine.h
        src/core/audio/bass/BassEngine.h
        src/core/audio/bass/BassEngine.cpp
        src/core/audio/miniaudio/miniaudio_impl.cpp
        src/core/audio/miniaudio/MiniaudioEngine.h
        src/core/audio/miniaudio/MiniaudioEngine.cpp

        # Playlist
        src/core/playlist/PlaylistManager.h
        src/core/playlist/PlaylistManager.cpp

        # VK
        src/core/vk/api/Client.h
        src/core/vk/api/Client.cpp
        src/core/vk/auth/Manager.h
        src/core/vk/auth/Manager.cpp

        # Lyrics
        src/core/lyrics/LyricsFetcher.h
        src/core/lyrics/LyricsFetcher.cpp

        # --- Services ---
        src/services/network/NetworkStreamer.h
        src/services/network/NetworkStreamer.cpp
        src/services/database/DatabaseManager.h
        src/services/database/DatabaseManager.cpp
        src/services/downloader/TrackDownloader.h
        src/services/downloader/TrackDownloader.cpp

        # --- UI ---
        src/ui/console/ConsoleController.h
        src/ui/console/ConsoleController.cpp

        # --- Utils ---
        src/utils/buffer/RingBuffer.h
        src/utils/buffer/RingBuffer.cpp
        src/utils/logger/Logger.h
        src/utils/logger/Logger.cpp
)

# --- Подключаем внешние библиотеки ---
add_subdirectory(libs/fdk-aac)

# Создаем исполняемый файл
add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        libs/tiny-aes/aes.c)

# Привязываем путь к заголовкам
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/fdk-aac/libAACdec/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/tiny-aes
)

# Линкуем модули Qt с приложением
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::Sql
)

# --- Платформозависимая линковка BASS ---

if (WIN32)
    # Для Windows ищем .lib файлы
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/bass.lib
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/basshls.lib
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/bassenc.lib
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/bassenc_mp3.lib
            fdk-aac
    )
elseif (UNIX AND NOT APPLE)
    # Для Linux ищем .so файлы (Shared Objects)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/libbass.so
            ${CMAKE_CURRENT_SOURCE_DIR}/libs/bass/libbasshls.so
    )
endif()