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

# Ищем базовые модули Qt
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql Qml WebView Concurrent)

# Указываем путь к исходникам и заголовкам
set(COMMON_SOURCES
        # --- Models ---
        src/models/Track.h

        # --- Core ---

        src/core/ApplicationCore.cpp
        src/core/ApplicationCore.h

        # API
        src/core/api/IAudioProvider.h

        # - Spotify -
        src/core/api/spotify/SpotifyClient.h
        src/core/api/spotify/SpotifyClient.cpp

        # - SoundCloud -
        src/core/api/soundcloud/SoundCloudClient.cpp
        src/core/api/soundcloud/SoundCloudClient.h

        # - VK -
        src/core/api/vk/VkClient.h
        src/core/api/vk/VkClient.cpp
        src/core/api/vk/VkDeviceManager.h
        src/core/api/vk/VkDeviceManager.cpp
        src/core/api/vk/VkAuthService.h
        src/core/api/vk/VkAuthService.cpp

        # - Yandex -
        src/core/api/yandex/YandexClient.cpp
        src/core/api/yandex/YandexClient.h

        # - YouTube -
        src/core/api/youtube/YouTubeClient.cpp
        src/core/api/youtube/YouTubeClient.h
        src/core/api/youtube/YouTubePoTokenGenerator.cpp
        src/core/api/youtube/YouTubePoTokenGenerator.h
        src/core/api/youtube/YouTubeExtractor.cpp
        src/core/api/youtube/YouTubeExtractor.h

        src/core/api/BaseApiProvider.cpp
        src/core/api/BaseApiProvider.h

        # Audio
        src/core/audio/miniaudio/miniaudio_impl.cpp

        src/core/audio/miniaudio/MiniaudioEngine.h
        src/core/audio/miniaudio/MiniaudioEngine.cpp

        src/core/audio/fourierTransform/FourierTransform.cpp
        src/core/audio/fourierTransform/FourierTransform.h

        src/core/audio/IAudioEngine.h

        src/core/audio/playback/PlaybackController.h
        src/core/audio/playback/PlaybackController.cpp

        # Authorization
        src/core/auth/oauth/OAuthManager.h
        src/core/auth/oauth/OAuthManager.cpp
        src/core/auth/oauth/WebViewCookieReader.h
        src/core/auth/oauth/WebViewCookieReader.cpp

        src/core/auth/router/SourceRouter.h
        src/core/auth/router/SourceRouter.cpp

        src/resources.qrc

        # Playlist
        src/core/playlist/PlaylistManager.h
        src/core/playlist/PlaylistManager.cpp

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

        src/services/config/ConfigurationService.h
        src/services/config/ConfigurationService.cpp
        src/services/session/PlaybackSessionService.h
        src/services/session/PlaybackSessionService.cpp

        # --- Shazam ---
        src/core/shazam/ShazamFFI.h
        src/core/shazam/IAudioCaptureService.h
        src/core/shazam/AudioCaptureService.h
        src/core/shazam/AudioCaptureService.cpp

        # --- UI ---
        src/ui/console/core/ConsoleController.h
        src/ui/console/core/ConsoleController.cpp

        src/ui/console/commands/CommandDispatcher.cpp
        src/ui/console/commands/CommandDispatcher.h
        src/ui/console/commands/PlaybackCommands.h
        src/ui/console/commands/PlaybackCommands.cpp
        src/ui/console/commands/VolumeCommands.h
        src/ui/console/commands/VolumeCommands.cpp
        src/ui/console/commands/PlaylistCommands.h
        src/ui/console/commands/PlaylistCommands.cpp
        src/ui/console/commands/SystemCommands.h
        src/ui/console/commands/SystemCommands.cpp

        src/ui/console/view/ConsoleRenderer.cpp
        src/ui/console/view/ConsoleRenderer.h

        src/ui/console/view/UltimateRenderer.cpp
        src/ui/console/view/UltimateRenderer.h

        # --- Utils ---
        src/utils/buffer/RingBuffer.h
        src/utils/buffer/RingBuffer.cpp

        src/utils/env/EnvParser.cpp
        src/utils/env/EnvParser.h

        src/utils/logger/Logger.h
        src/utils/logger/Logger.cpp

        src/utils/parser/MpegTsDemuxer.cpp
        src/utils/parser/MpegTsDemuxer.h
        src/utils/parser/Id3Utils.h

        src/utils/platform/IDialogService.h
        src/utils/platform/DialogService.h
        src/utils/platform/DialogService.cpp

        src/utils/path/PathManager.h
        src/utils/path/PathManager.cpp
)

set(PROJECT_SOURCES
        src/main.cpp
        ${COMMON_SOURCES}
)

# --- Подключаем внешние библиотеки ---
add_subdirectory(libs/fdk-aac)

# Подключаем безопасное хранилище QtKeychain
include(FetchContent)
FetchContent_Declare(
        qtkeychain
        GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
        GIT_TAG        0.17.0
)
FetchContent_MakeAvailable(qtkeychain)

# Создаем исполняемый файл
add_executable(${PROJECT_NAME}
        ${PROJECT_SOURCES}
        libs/tiny-aes/aes.c)

# Привязываем путь к заголовкам
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/fdk-aac/libAACdec/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/tiny-aes
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/minimp3
)

# Линкуем модули Qt с приложением
target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::Sql
        Qt6::Qml
        Qt6::WebView
        qt6keychain
        Qt6::Concurrent
)

# --- Внешние библиотеки и FFI ---

target_link_libraries(${PROJECT_NAME} PRIVATE fdk-aac)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll.lib"
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            # Копируем DLL Шазама
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Включение аппаратного ускорения AVX2
if(MSVC)
    target_compile_options(VKAudioPlayer PRIVATE
            $<$<CONFIG:Release>:/O2>
            /arch:AVX2
            /fp:fast
    )
else()
    target_compile_options(VKAudioPlayer PRIVATE
            $<$<CONFIG:Release>:-O3>
            -mavx2
            -ffast-math
    )
endif()