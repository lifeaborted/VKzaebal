#include <iostream>
#include <string>
#include <atomic>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QSettings>

#include "core/audio/AudioEngine/AudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "core/vk/VkApiClient/VkApiClient.h"
#include "core/vk/VkAuthManager/VkAuthManager.h"
#include "utils/ConsoleController/ConsoleController.h"
#include "utils/logger/logger.h"
#include "utils/DatabaseManager/DatabaseManager.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QGuiApplication app(argc, argv);
    Logger::Init();
    Logger::Log(LogLevel::INFO, "--- VK Audio Player Started ---");

    // Инициализация QSettings (создаст config.ini)
    QSettings settings("config.ini", QSettings::IniFormat);

    DatabaseManager dbManager;
    if (!dbManager.Init()) return -1;

    AudioEngine audio;
    if (!audio.Init()) return -1;

    PlaylistManager playlist;
    VkApiClient vkClient;
    VkAuthManager authManager;

    ConsoleController console(audio, playlist, authManager, dbManager);
    QObject::connect(&console, &ConsoleController::QuitRequested, &app, &QCoreApplication::quit);

    // --- ЛОГИКА ВОСПРОИЗВЕДЕНИЯ И КЭШИРОВАНИЯ ---
    audio.OnTrackFinished = [&]() { playlist.Next(); };

    std::string prefetchedTrackId = "";
    std::string prefetchedUrl = "";
    std::atomic<int> playbackGeneration{0};

    std::function<void(Track, int)> attemptPlay;
    attemptPlay = [&](Track track, int attempt) {
        int currentGen = (attempt == 1) ? ++playbackGeneration : playbackGeneration.load();
        Logger::Log(LogLevel::INFO, ">>> Requesting URL for: " + track.artist + " - " + track.title + " (Attempt " + std::to_string(attempt) + "/3)");

        auto executePlay = [track, attempt, currentGen, &audio, &playlist, &prefetchedTrackId, &prefetchedUrl, &vkClient, &attemptPlay, &playbackGeneration](const std::string& freshUrl, bool isNetworkError) {

            if (currentGen != playbackGeneration.load()) {
                Logger::Log(LogLevel::INFO, "Main: Ignored outdated response for " + track.title);
                return;
            }

            if (!isNetworkError && freshUrl.empty()) {
                Logger::Log(LogLevel::WARNING, "Track is restricted or deleted. Skipping immediately.");
                playlist.Next();
                return;
            }

            bool playSuccess = false;
            if (!freshUrl.empty()) {
                playSuccess = audio.PlayStream(freshUrl);
            }

            if (playSuccess) {
                Logger::Log(LogLevel::INFO, ">>> Playing: " + track.artist + " - " + track.title + " [" + track.GetFormattedDuration() + "]");
                prefetchedTrackId = "";
                prefetchedUrl = "";

                Track nextTrack = playlist.GetNextTrackPreview();
                if (!nextTrack.id.empty()) {
                    prefetchedTrackId = nextTrack.id;
                    Logger::Log(LogLevel::INFO, "Background API request for next track: " + nextTrack.title);

                    vkClient.FetchTrackUrl(nextTrack.id, [&audio, &prefetchedUrl, currentGen, &playbackGeneration](const std::string& nextUrl, bool nextNetError) {
                        if (currentGen != playbackGeneration.load()) return;
                        if (!nextNetError && !nextUrl.empty()) {
                            prefetchedUrl = nextUrl;
                            audio.PreloadStream(nextUrl);
                        }
                    });
                }
                return;
            }

            Logger::Log(LogLevel::WARNING, "Playback or network failed for track: " + track.title);
            if (attempt < 3) {
                Logger::Log(LogLevel::INFO, "Retrying in 2 seconds...");
                QTimer::singleShot(2000, [track, attempt, &attemptPlay]() { attemptPlay(track, attempt + 1); });
            } else {
                Logger::Log(LogLevel::ERROR, "Max retries reached. Skipping track.");
                playlist.Next();
            }
        };

        if (track.id == prefetchedTrackId && !prefetchedUrl.empty()) {
            Logger::Log(LogLevel::INFO, "Main: Cache hit! Using prefetched URL.");
            executePlay(prefetchedUrl, false);
        } else {
            vkClient.FetchTrackUrl(track.id, executePlay);
        }
    };

    // Привязываем функцию воспроизведения к менеджеру плейлиста
    playlist.OnTrackRequested = [&](Track track) { attemptPlay(track, 1); };

    // --- ЛОГИКА АВТОРИЗАЦИИ И СКАЧИВАНИЯ ---
    QObject::connect(&vkClient, &VkApiClient::AudioFetched, [&](const std::vector<Track>& tracks) {
        bool isFirstChunk = !playlist.HasTracks();
        for (const auto& track : tracks) playlist.AddTrack(track);
        std::cout << "[Плейлист] Загружена порция из " << tracks.size() << " треков.\n";

        // Кэшируем треки в БД
        dbManager.SaveTracks(tracks);

        if (isFirstChunk && playlist.HasTracks()) {
            // Сохраняем очередь и делаем экспорт в TXT
            dbManager.SaveQueue(playlist.GetQueueTracks(), playlist.IsShuffle());
            dbManager.ExportQueueToTxt("playlist.txt", playlist.IsShuffle());

            std::cout << "\n=== ПЕРВАЯ ЧАСТЬ ЗАГРУЖЕНА. НАЧИНАЕМ ВОСПРОИЗВЕДЕНИЕ ===\n\n"
                      << "Controls:\n [P] Play/Pause\n [N] Next\n [B] Prev\n"
                      << " [+] Vol Up\n [-] Vol Down\n [S] Shuffle\n [R] Repeat Mode\n"
                      << " [J <num>] Jump to track\n [Q] Quit\n-----------------------\n> ";

            // Теперь эта строка сработает идеально!
            playlist.OnTrackRequested(playlist.GetCurrentTrack());
        }
    });

    QObject::connect(&authManager, &VkAuthManager::TokenReceived, [&](const std::string& token) {
        settings.setValue("vk_token", QString::fromStdString(token));
        vkClient.SetAccessToken(token);
        console.SetState(ConsoleState::COMMAND_MODE);
        std::cout << "\n[УСПЕХ] Авторизация пройдена! Загрузка аудио...\n> ";
        vkClient.FetchAllUserAudio(0, 200);
    });

    std::function<void()> startAuthFlow = [&]() {
        Logger::Log(LogLevel::INFO, "Main: Starting auth flow. Opening default OS browser...");
        std::cout << "\n=== Авторизация ВКонтакте ===\n";
        std::cout << "СКОПИРУЙТЕ всю ссылку из адресной строки пустой страницы и вставьте ее сюда:\n> ";

        QString authUrl = "https://id.vk.ru/auth?return_auth_hash=635629c60a8045eda3&redirect_uri=https%3A%2F%2Foauth.vk.ru%2Fblank.html&redirect_uri_hash=0524b4bb1fe4331621&force_hash=1&app_id=6287487&response_type=token&code_challenge=&code_challenge_method=&scope=408861919&state=";
        QDesktopServices::openUrl(QUrl(authUrl));

        console.SetState(ConsoleState::WAITING_TOKEN_URL);
    };

    QObject::connect(&vkClient, &VkApiClient::TokenExpired, [&]() {
        Logger::Log(LogLevel::WARNING, "Main: Token expired.");
        std::cout << "\n[ВНИМАНИЕ] Токен устарел.\n";
        settings.remove("vk_token");
        vkClient.SetAccessToken("");
        startAuthFlow();
    });

    // --- ПЕРВИЧНЫЙ ЗАПУСК ---
    QString savedToken = settings.value("vk_token", "").toString();

    if (savedToken.isEmpty()) {
        startAuthFlow();
    } else {
        vkClient.SetAccessToken(savedToken.toStdString());
        std::cout << "Проверка сохраненного токена...\n";

        vkClient.ValidateToken([&, startAuthFlow](bool isValid) {
            if (isValid) {
                console.SetState(ConsoleState::COMMAND_MODE);
                std::cout << "\n[УСПЕХ] Токен действителен! Загрузка аудио...\n> ";
                vkClient.FetchAllUserAudio(0, 200);
            } else {
                std::cout << "\n[ВНИМАНИЕ] Сохраненный токен недействителен.\n";
                settings.remove("vk_token");
                vkClient.SetAccessToken("");
                startAuthFlow();
            }
        });
    }

    // Запускаем консольный поток
    console.Start();

    int exitCode = app.exec();

    Logger::Close();
    return exitCode;
}