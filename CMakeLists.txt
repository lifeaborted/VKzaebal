cmake_minimum_required(VERSION 3.16)
project(VKAudioPlayer VERSION 0.1 LANGUAGES C CXX)

# Задаем стандарт C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опции модульной сборки интерфейса
option(ENABLE_FTXUI "Build with modern FTXUI terminal interface" ON)

# Включаем автоматическую генерацию мета-кода Qt глобально
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Автоматическое определение путей к Qt6 (если CMAKE_PREFIX_PATH не задан вручную)
if(NOT CMAKE_PREFIX_PATH)
    if(DEFINED ENV{QTDIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
    elseif(DEFINED ENV{Qt6_DIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{Qt6_DIR}")
    elseif(EXISTS "C:/Qt/6.11.1/msvc2022_64")
        list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64")
    elseif(EXISTS "C:/Qt/6.8.0/msvc2022_64")
        list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.0/msvc2022_64")
    elseif(EXISTS "C:/Qt/6.7.2/msvc2022_64")
        list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.7.2/msvc2022_64")
    endif()
endif()


if(WIN32)
    set(Qt6WebEngineQuick_FOUND TRUE CACHE BOOL "" FORCE)
    set(Qt6WebEngineCore_FOUND TRUE CACHE BOOL "" FORCE)
    if(NOT TARGET Qt6::WebEngineQuick)
        add_library(Qt6::WebEngineQuick INTERFACE IMPORTED)
    endif()
    if(NOT TARGET Qt6::WebEngineCore)
        add_library(Qt6::WebEngineCore INTERFACE IMPORTED)
    endif()
endif()

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
        src/core/auth/router/UnifiedSearchAggregator.h
        src/core/auth/router/UnifiedSearchAggregator.cpp

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
        src/services/database/repositories/TrackRepository.h
        src/services/database/repositories/TrackRepository.cpp
        src/services/database/repositories/PlaylistRepository.h
        src/services/database/repositories/PlaylistRepository.cpp
        src/services/database/repositories/SessionRepository.h
        src/services/database/repositories/SessionRepository.cpp

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

        # --- UI Interface & Factory ---
        src/ui/IUiController.h
        src/ui/UiFactory.h
        src/ui/UiFactory.cpp

        # --- CLI ---
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

        src/ui/console/light/LightConsoleController.h
        src/ui/console/light/LightConsoleController.cpp

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

# --- TUI (FTXUI) Источники (опционально) ---
if(ENABLE_FTXUI)
    set(FTXUI_SOURCES
        src/ui/tui/core/TuiModalManager.h
        src/ui/tui/core/TuiModalManager.cpp
        src/ui/tui/core/TuiCommandBar.h
        src/ui/tui/core/TuiCommandBar.cpp
        src/ui/tui/core/TuiBottomBar.h
        src/ui/tui/core/TuiBottomBar.cpp
        src/ui/tui/core/TuiInputRouter.h
        src/ui/tui/core/TuiInputRouter.cpp
        src/ui/tui/modals/IModalDialog.h
        src/ui/tui/utils/TuiStringUtils.h
        src/ui/tui/utils/TuiStringUtils.cpp
        src/ui/tui/services/TuiTrackService.h
        src/ui/tui/services/TuiTrackService.cpp

        src/ui/tui/TuiTheme.h
        src/ui/tui/TuiThemeConfig.h
        src/ui/tui/TuiThemeConfig.cpp
        src/ui/tui/CoverArtRenderer.h
        src/ui/tui/CoverArtRenderer.cpp
        src/ui/tui/components/SidebarComponent.h
        src/ui/tui/components/SidebarComponent.cpp
        src/ui/tui/components/PlaylistModalComponent.h
        src/ui/tui/components/PlaylistModalComponent.cpp
        src/ui/tui/components/AddToPlaylistModalComponent.h
        src/ui/tui/components/AddToPlaylistModalComponent.cpp
        src/ui/tui/components/EqualizerModalComponent.h
        src/ui/tui/components/EqualizerModalComponent.cpp
        src/ui/tui/components/VkLoginModalComponent.h
        src/ui/tui/components/VkLoginModalComponent.cpp
        src/ui/tui/modals/settings/ISettingsTab.h
        src/ui/tui/modals/settings/GeneralSettingsTab.h
        src/ui/tui/modals/settings/GeneralSettingsTab.cpp
        src/ui/tui/modals/settings/PlaybackSettingsTab.h
        src/ui/tui/modals/settings/PlaybackSettingsTab.cpp
        src/ui/tui/modals/settings/VisualizerSettingsTab.h
        src/ui/tui/modals/settings/VisualizerSettingsTab.cpp
        src/ui/tui/modals/settings/ThemeSettingsTab.h
        src/ui/tui/modals/settings/ThemeSettingsTab.cpp
        src/ui/tui/modals/settings/ServiceColorsSettingsTab.h
        src/ui/tui/modals/settings/ServiceColorsSettingsTab.cpp
        src/ui/tui/components/SettingsModalComponent.h
        src/ui/tui/components/SettingsModalComponent.cpp
        src/ui/tui/components/HelpModalComponent.h
        src/ui/tui/components/HelpModalComponent.cpp
        src/ui/tui/screens/NowPlayingScreen.h
        src/ui/tui/screens/NowPlayingScreen.cpp
        src/ui/tui/screens/SearchScreen.h
        src/ui/tui/screens/SearchScreen.cpp
        src/ui/tui/TuiController.h
        src/ui/tui/TuiController.cpp
    )
endif()

set(PROJECT_SOURCES
        src/main.cpp
        ${COMMON_SOURCES}
        ${FTXUI_SOURCES}
)

# --- Подключаем внешние библиотеки ---

# 1. fdk-aac: проверяем Conan-пакет или локальную папку libs/fdk-aac
find_package(libfdk_aac QUIET CONFIG)
if(TARGET libfdk_aac::libfdk_aac)
    set(FDK_AAC_LIB libfdk_aac::libfdk_aac)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/fdk-aac/CMakeLists.txt")
    add_subdirectory(libs/fdk-aac)
    set(FDK_AAC_LIB fdk-aac)
endif()

# 2. Подключаем безопасное хранилище QtKeychain (статически)
find_package(qt6keychain QUIET CONFIG)
if(NOT TARGET qt6keychain)
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
            qtkeychain
            GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
            GIT_TAG        0.17.0
    )
    FetchContent_MakeAvailable(qtkeychain)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
endif()

# 3. Подключаем FTXUI для нативного TUI интерфейса (проверяем Conan или FetchContent)
if(ENABLE_FTXUI)
    find_package(ftxui QUIET CONFIG)
    if(NOT TARGET ftxui::screen)
        set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        include(FetchContent)
        FetchContent_Declare(
                ftxui
                GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
                GIT_TAG        v5.0.0
        )
        FetchContent_MakeAvailable(ftxui)
        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
    endif()
endif()

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

# Линкуем базовые модули Qt с приложением
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

# Подключение FTXUI к приложению (если включено)
if(ENABLE_FTXUI)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_FTXUI)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ftxui::screen
        ftxui::dom
        ftxui::component
    )
endif()

# --- Внешние библиотеки и FFI ---

if(FDK_AAC_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FDK_AAC_LIB})
endif()

if (WIN32)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll.lib")
        target_link_libraries(${PROJECT_NAME} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll.lib"
        )

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endif()
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

# Включение межпроцедурной оптимизации (LTO/LTCG) для Release конфигурации
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# ==============================================================================
# Qt CMake Deployment API (Qt 6.3+)
# ==============================================================================
if(WIN32 AND NOT DEFINED CMAKE_INSTALL_BINDIR)
    set(CMAKE_INSTALL_BINDIR ".")
endif()
include(GNUInstallDirs)

install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
        TARGET ${PROJECT_NAME}
        OUTPUT_SCRIPT deploy_script
        NO_TRANSLATIONS
        NO_COMPILER_RUNTIME
        NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

if(WIN32)
    # fdk-aac DLL
    if(TARGET fdk-aac)
        get_target_property(fdkaac_type fdk-aac TYPE)
        if(fdkaac_type STREQUAL "SHARED_LIBRARY")
            install(FILES $<TARGET_FILE:fdk-aac> DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endif()

    # qt6keychain DLL (если собран динамически)
    if(TARGET qt6keychain)
        get_target_property(qt6keychain_type qt6keychain TYPE)
        if(qt6keychain_type STREQUAL "SHARED_LIBRARY")
            install(FILES $<TARGET_FILE:qt6keychain> DESTINATION ${CMAKE_INSTALL_BINDIR})
        endif()
    endif()

    # Shazam FFI DLL
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/shazam_ffi/target/release/shazam_ffi.dll" DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()

# Установка конфигурационных файлов и ресурсов
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ftxui.cfg" DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config.ini" DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/.env" DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/covers")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/covers" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()