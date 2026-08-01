#include <iostream>
#include <string>
#include <thread>
#include <QCoreApplication>
#include <QMetaObject>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "bass.h"

#ifdef _WIN32
#undef ERROR
#endif

#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "utils/logger/logger.h"

HSTREAM g_CurrentStream = 0;
float g_Volume = 1.0f; // Громкость от 0.0 до 1.0

std::string GetEnvVar(const std::string& filePath, const std::string& key) {
    std::ifstream file(filePath);
    if (!file.is_open()) return "";
    std::string line, searchKey = key + "=";
    while (std::getline(file, line)) {
        if (line.find(searchKey) == 0) {
            std::string value = line.substr(searchKey.length());
            value.erase(0, value.find_first_not_of(" \n\r\t"));
            value.erase(value.find_last_not_of(" \n\r\t") + 1);
            return value;
        }
    }
    return "";
}

// Коллбэк BASS, вызываемый по завершении трека
void CALLBACK OnTrackEnd(HSYNC handle, DWORD channel, DWORD data, void *user) {
    Logger::Log(LogLevel::INFO, "--- TRACK COMPLETION EVENT RECEIVED ---");
    PlaylistManager* playlist = static_cast<PlaylistManager*>(user);

    // Передаем команду в главный поток Qt для безопасного переключения
    QMetaObject::invokeMethod(QCoreApplication::instance(), [playlist]() {
        playlist->Next();
    }, Qt::QueuedConnection);
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    Logger::Init();
    QCoreApplication app(argc, argv);
    Logger::Log(LogLevel::INFO, "--- VK Audio Engine (BASS) Started ---");

    // ==========================================
    // 0. ИНИЦИАЛИЗАЦИЯ BASS
    // ==========================================

    // Инициализируем аудиоустройство по умолчанию (44100 Гц, стерео)
    if (!BASS_Init(-1, 44100, 0, 0, NULL)) {
        Logger::Log(LogLevel::ERROR, "Failed to initialize BASS. Error code: " + std::to_string(BASS_ErrorGetCode()));
        return -1;
    }

    BASS_SetConfig(BASS_CONFIG_NET_TIMEOUT, 5000);

    BASS_SetConfig(BASS_CONFIG_NET_PLAYLIST, 1);

    // Загружаем плагин BASS_HLS для поддержки .m3u8
    HPLUGIN hlsPlugin = BASS_PluginLoad("basshls.dll", 0);
    if (hlsPlugin == 0) {
        Logger::Log(LogLevel::ERROR, "Failed to load basshls.dll! Make sure it is in the same directory as the executable.");
    } else {
        Logger::Log(LogLevel::INFO, "BASS_HLS Plugin successfully loaded.");
    }

    PlaylistManager playlist;
    VkApiClient vkClient;

// ==========================================
    // 1. НАСТРОЙКА МЕНЕДЖЕРА ПЛЕЙЛИСТА
    // ==========================================

    playlist.OnTrackRequested = [&](const Track& track) {
        Logger::Log(LogLevel::INFO, ">>> Requesting fresh URL for: " + track.artist + " - " + track.title);

        // Асинхронно запрашиваем свежую ссылку по ID трека
        vkClient.FetchTrackUrl(track.id, [&playlist, track](const std::string& freshUrl) {
            if (freshUrl.empty()) {
                Logger::Log(LogLevel::ERROR, "Failed to fetch fresh URL for track. Skipping...");
                playlist.Next();
                return;
            }

            // Освобождаем предыдущий поток, если он был
            if (g_CurrentStream != 0) {
                BASS_StreamFree(g_CurrentStream);
                g_CurrentStream = 0;
            }

            // Создаем новый поток со свежей ссылкой
            g_CurrentStream = BASS_StreamCreateURL(freshUrl.c_str(), 0, BASS_STREAM_AUTOFREE, NULL, NULL);

            if (g_CurrentStream != 0) {
                BASS_ChannelSetAttribute(g_CurrentStream, BASS_ATTRIB_VOL, g_Volume);
                BASS_ChannelSetSync(g_CurrentStream, BASS_SYNC_END, 0, OnTrackEnd, &playlist);
                BASS_ChannelPlay(g_CurrentStream, FALSE);

                Logger::Log(LogLevel::INFO, ">>> Playing: " + track.artist + " - " + track.title + " [" + track.GetFormattedDuration() + "]");
            } else {
                int errorCode = BASS_ErrorGetCode();
                Logger::Log(LogLevel::ERROR, "BASS failed to create stream! Error code: " + std::to_string(errorCode));
                playlist.Next(); // Пропускаем сломанный трек
            }
        });
    };

    // ==========================================
    // 2. ИНТЕГРАЦИЯ VK API
    // ==========================================

    std::string myVkToken = GetEnvVar(".env", "VK_TOKEN");
    if (!myVkToken.empty()) {
        vkClient.SetAccessToken(myVkToken);
    } else {
        Logger::Log(LogLevel::ERROR, "VK Token is missing!");
    }

    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        std::cout << "\n=== ПЛЕЙЛИСТ VK ЗАГРУЖЕН ===" << std::endl;
        for (const auto& track : tracks) {
            playlist.AddTrack(track); // Закидываем сам объект Track
        }
        std::cout << "Added " << tracks.size() << " tracks." << std::endl;

        if (playlist.HasTracks()) {
            playlist.OnTrackRequested(playlist.GetCurrentTrack());
        }
    });

    vkClient.FetchUserAudio(0, 50);

    // ==========================================
    // 3. КОНСОЛЬНОЕ УПРАВЛЕНИЕ
    // ==========================================

    std::cout << "\nControls:\n [P] Play/Pause\n [N] Next Track\n [B] Previous Track\n [+] Volume Up\n [-] Volume Down\n [Q] Quit\n-----------------------\n";

    std::thread inputThread([&]() {
        char command;
        bool isRunning = true;
        while (isRunning) {
            std::cin >> command;
            command = std::tolower(command);

            switch (command) {
                case 'p':
                    if (g_CurrentStream != 0) {
                        if (BASS_ChannelIsActive(g_CurrentStream) == BASS_ACTIVE_PLAYING) {
                            BASS_ChannelPause(g_CurrentStream);
                            Logger::Log(LogLevel::INFO, "[PAUSED]");
                        } else {
                            BASS_ChannelPlay(g_CurrentStream, FALSE);
                            Logger::Log(LogLevel::INFO, "[PLAYING]");
                        }
                    }
                    break;
                case 'n':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Next(); }, Qt::QueuedConnection);
                    break;
                case 'b':
                    QMetaObject::invokeMethod(&app, [&]() { playlist.Previous(); }, Qt::QueuedConnection);
                    break;
                case '+':
                    g_Volume = std::min(1.0f, g_Volume + 0.1f);
                    if (g_CurrentStream) BASS_ChannelSetAttribute(g_CurrentStream, BASS_ATTRIB_VOL, g_Volume);
                    Logger::Log(LogLevel::INFO, "[Volume]: " + std::to_string(static_cast<int>(g_Volume * 100)) + "%");
                    break;
                case '-':
                    g_Volume = std::max(0.0f, g_Volume - 0.1f);
                    if (g_CurrentStream) BASS_ChannelSetAttribute(g_CurrentStream, BASS_ATTRIB_VOL, g_Volume);
                    Logger::Log(LogLevel::INFO, "[Volume]: " + std::to_string(static_cast<int>(g_Volume * 100)) + "%");
                    break;
                case 'q':
                    isRunning = false;
                    Logger::Log(LogLevel::INFO, "Quit command received.");
                    QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
                    break;
            }
        }
    });

    int exitCode = app.exec();

    if (inputThread.joinable()) {
        inputThread.join();
    }

    // Освобождаем BASS перед закрытием
    BASS_Free();
    Logger::Close();
    return exitCode;
}