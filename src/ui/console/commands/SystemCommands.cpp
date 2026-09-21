#include "SystemCommands.h"
#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "utils/platform/IDialogService.h"
#include "core/shazam/IAudioCaptureService.h"
#include "core/shazam/ShazamFFI.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QThreadPool>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

#include <cstdio>
#include <vector>
#include <string>

namespace {
    void RunInMainThread(std::function<void()> func) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), func, Qt::QueuedConnection);
    }

    class DownloadControlCommand : public IConsoleCommand {
        bool m_isRemove;
    public:
        explicit DownloadControlCommand(bool isRemove) : m_isRemove(isRemove) {}
        void Execute(const std::string& arg, CommandContext& ctx) override {
            Track targetTrack;
            bool isValid = false;

            if (arg.empty()) {
                targetTrack = ctx.playlist.GetCurrentTrack();
                isValid = true;
            } else {
                try {
                    int idx = std::stoi(arg) - 1;
                    std::vector<Track> queue = ctx.playlist.GetQueueTracks();
                    if (idx >= 0 && idx < static_cast<int>(queue.size())) {
                        targetTrack = queue[idx];
                        isValid = true;
                    }
                } catch(...) {}
            }

            if (isValid && !targetTrack.id.empty()) {
                QString pathMp3 = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "mp3");
                QString pathAac = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "aac");

                if (!m_isRemove) {
                    if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                        if (ctx.print) ctx.print("[Загрузка] Трек уже скачан.\n\n> ");
                    } else {
                        if (!ctx.currentProvider) {
                            if (ctx.print) ctx.print("[Ошибка] Нет активного онлайн-источника для скачивания.\n\n> ");
                            return;
                        }

                        QString customDir = PathManager::GetCustomDownloadsDir();
                        QString targetDir = customDir;

                        if (targetDir.isEmpty()) {
                            std::string nativeFolder = ctx.dialogService.ChooseFolderDialog("Выберите папку для сохранения аудио");
                            targetDir = QString::fromStdString(nativeFolder);

                            if (targetDir.isEmpty()) {
                                if (ctx.print) ctx.print("[Загрузка] Скачивание отменено (папка не выбрана).\n\n> ");
                                return;
                            }
                            PathManager::SetSessionDownloadsDir(targetDir);
                        }

                        QString targetMp3 = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "mp3", targetDir);
                        QString targetAac = PathManager::GetDownloadFilePath(targetTrack.GetSafeFilename(), "aac", targetDir);
                        if (QFile::exists(targetMp3) || QFile::exists(targetAac)) {
                            if (ctx.print) ctx.print("[Загрузка] Трек уже скачан в выбранную папку.\n\n> ");
                            return;
                        }

                        if (ctx.print) ctx.print("[Загрузка] Получение ссылки для " + targetTrack.title + "...\n\n> ");

                        RunInMainThread([provider = ctx.currentProvider, downloader = &ctx.downloader, targetTrack = std::move(targetTrack), targetDir = std::move(targetDir)]() {
                            if (!provider) return;
                            provider->FetchTrackUrl(targetTrack.id, [downloader, targetTrack = std::move(targetTrack), targetDir = std::move(targetDir)](const std::string& url, bool err) {
                                if (!err && !url.empty()) downloader->Download(targetTrack, url, targetDir);
                                else Logger::Log(LogLevel::WARNING, "Failed to get URL for download.");
                            });
                        });
                    }
                } else {
                    if (QFile::exists(pathMp3) || QFile::exists(pathAac)) {
                        QFile::remove(pathMp3);
                        QFile::remove(pathAac);
                        if (ctx.print) ctx.print("[Кэш] Удален: " + targetTrack.artist + " - " + targetTrack.title + "\n\n> ");
                    } else {
                        if (ctx.print) ctx.print("[Кэш] Трек не был скачан.\n\n> ");
                    }
                }
            } else {
                if (ctx.print) ctx.print("[Ошибка] Не удалось найти трек.\n\n> ");
            }
        }
    };

    class LyricsCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            Track currentTrack = ctx.playlist.GetCurrentTrack();
            if (currentTrack.id.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Сейчас никакой трек не играет.\n\n> ");
                return;
            }

            auto showLyricsFile = [&ctx, &currentTrack](const std::string& text) {
                if (text.empty()) {
                    if (ctx.print) ctx.print("[Текст] Текст для данного трека не найден.\n\n> ");
                    return;
                }
                bool isNewFile = false;
                QString filePath = PathManager::GetLyricsFilePath(currentTrack.artist, currentTrack.title, isNewFile);
                QFile file(filePath);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    file.write(QByteArray::fromStdString(text));
                    file.close();
                }
                QFileInfo fileInfo(filePath);
                QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absoluteFilePath()));
                if (ctx.print) ctx.print("[Текст] Открыт файл: " + filePath.toStdString() + "\n\n> ");
            };

            std::string text = currentTrack.lyrics;
            if (text.empty()) {
                if (ctx.print) ctx.print("[Текст] Поиск текста...\n\n> ");
                RunInMainThread([lyricsFetcher = &ctx.lyricsFetcher, db = &ctx.dbManager, currentTrack = std::move(currentTrack), showLyricsFile]() {
                    lyricsFetcher->FetchLyrics(currentTrack.artist, currentTrack.title, [db, currentTrack, showLyricsFile](const std::string& fetchedText) {
                        if (!fetchedText.empty()) db->UpdateTrackLyrics(currentTrack.id, fetchedText);
                        showLyricsFile(fetchedText);
                    });
                });
            } else {
                showLyricsFile(text);
            }
        }
    };

    class ShazamCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            std::string filePath = "";
            bool isMic = false;

            if (arg == "file") {
                filePath = ctx.dialogService.OpenAudioFileDialog();
                if (filePath.empty()) {
                    if (ctx.print) ctx.print("[Shazam] Отмена. Файл не выбран.\n\n> ");
                    return;
                }
                if (ctx.print) ctx.print("[Shazam] Выбран файл: " + filePath + "\n> ");
            } else if (arg == "mic") {
                isMic = true;
                filePath = ctx.audioCapture.GetDefaultRecordPath();
                if (ctx.print) ctx.print("[Shazam] \xF0\x9F\x8E\xA4 Запись с микрофона (7 секунд)...\n> ");
            } else if (!arg.empty()) {
                filePath = arg;
            } else {
                if (ctx.print) ctx.print("\n=== Выбор источника ===\n1 - ВКонтакте\n2 - Spotify\n3 - SoundCloud\n4 - Yandex\n5 - YouTube Music\n6 - Оффлайн режим\n\nВведите номер: ");
                return;
            }

            QThreadPool::globalInstance()->start([audioCapture = &ctx.audioCapture, print = ctx.print, netMgr = ctx.networkManager, filePath, isMic]() {
                if (isMic) {
                    if (!audioCapture->RecordToFile(filePath, 7)) {
                        RunInMainThread([print]() {
                            if (print) print("\n[Shazam] Ошибка захвата звука с микрофона.\n> ");
                        });
                        return;
                    }
                }

                RunInMainThread([print]() {
                    if (print) print("\n[Shazam] Анализ аудио...\n> ");
                });

                char* raw_base64 = generate_shazam_signature(filePath.c_str());

                if (isMic) {
                    std::remove(filePath.c_str());
                }

                if (!raw_base64) {
                    RunInMainThread([print]() {
                        if (print) print("\n[Shazam] Ошибка: Не удалось обработать аудио.\n> ");
                    });
                    return;
                }

                QString base64Sig = QString::fromUtf8(raw_base64);
                free_shazam_string(raw_base64);

                RunInMainThread([netMgr, print, base64Sig = std::move(base64Sig)]() {
                    QJsonObject sigObj{ {"uri", base64Sig}, {"samplems", 12000} };
                    QJsonObject rootObj{ {"signature", sigObj} };
                    QByteArray jsonPayload = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);

                    QNetworkAccessManager* manager = netMgr;
                    bool shouldDeleteManager = false;
                    if (!manager) {
                        manager = new QNetworkAccessManager();
                        shouldDeleteManager = true;
                    }

                    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    QUrl url("https://amp.shazam.com/discovery/v5/ru/RU/android/-/tag/" + uuid + "/" + uuid);

                    QNetworkRequest request(url);
                    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                    request.setRawHeader("User-Agent", "Shazam Android/13.7.0");

                    QNetworkReply* reply = manager->post(request, jsonPayload);

                    QObject::connect(reply, &QNetworkReply::finished, [print, reply, manager, shouldDeleteManager]() {
                        if (reply->error() == QNetworkReply::NoError) {
                            QJsonObject trackObj = QJsonDocument::fromJson(reply->readAll()).object()["track"].toObject();
                            if (trackObj.isEmpty()) {
                                if (print) print("\n[Shazam] Трек не распознан :( Возможно, его нет в базе.\n> ");
                            } else {
                                QString title = trackObj["title"].toString();
                                QString artist = trackObj["subtitle"].toString();
                                if (print) print("\n[Shazam] Найдено: " + artist.toStdString() + " - " + title.toStdString() + "\n> ");
                            }
                        } else {
                            if (print) print("\n[Shazam] Ошибка сети: " + reply->errorString().toStdString() + "\n> ");
                        }
                        reply->deleteLater();
                        if (shouldDeleteManager) {
                            manager->deleteLater();
                        }
                    });
                });
            });
        }
    };

    class ConfigCommand : public IConsoleCommand {
        std::string m_cmdType;
    public:
        explicit ConfigCommand(const std::string& type) : m_cmdType(type) {}
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (m_cmdType == "source") {
                if (ctx.onSourceChange) ctx.onSourceChange("SELECT");
            } else if (m_cmdType == "vis") {
                if (ctx.onVisualizerToggle) ctx.onVisualizerToggle();
            } else if (m_cmdType == "mode") {
                try {
                    int mode = std::stoi(arg);
                    if (mode == 0 || mode == 1) {
                        bool isGapless = (mode == 1);
                        if (ctx.onGaplessMode) {
                            RunInMainThread([cb = ctx.onGaplessMode, isGapless]() { cb(isGapless); });
                        }
                        if (ctx.print) ctx.print("[Режим] Установлен " + std::string(isGapless ? "плавный (gapless)" : "стандартный") + " переход.\n\n> ");
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ");
                    }
                } catch (...) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ");
                }
            } else if (m_cmdType == "reload") {
                if (ctx.onReloadUi) {
                    RunInMainThread([cb = ctx.onReloadUi]() { cb(); });
                }
            }
        }
    };

    class SystemCommand : public IConsoleCommand {
        std::string m_cmdType;
    public:
        explicit SystemCommand(const std::string& type) : m_cmdType(type) {}
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (m_cmdType == "logout") {
                if (arg == "vk" || arg == "spotify" || arg == "sc" || arg == "soundcloud" || arg == "yandex" || arg == "youtube" || arg == "yt" || arg == "all") {
                    if (ctx.onLogout) {
                        RunInMainThread([cb = ctx.onLogout, arg]() { cb(arg); });
                    }
                } else if (arg.empty()) {
                    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
                    QString currentSrc = settings.value("General/source", "").toString().toLower();
                    if (currentSrc == "vk" || currentSrc == "spotify" || currentSrc == "sc" || currentSrc == "soundcloud" || currentSrc == "yandex" || currentSrc == "youtube") {
                        if (ctx.onLogout) {
                            RunInMainThread([cb = ctx.onLogout, src = currentSrc.toStdString()]() { cb(src); });
                        }
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Укажите сервис: logout vk | logout spotify | logout sc | logout yandex | logout youtube | logout all\n\n> ");
                    }
                } else {
                    if (ctx.print) ctx.print("[Ошибка] Укажите сервис: logout vk | logout spotify | logout sc | logout yandex | logout youtube | logout all\n\n> ");
                }
            } else if (m_cmdType == "info") {
                RunInMainThread([pl = &ctx.playlist, print = ctx.print]() {
                    Track current = pl->GetCurrentTrack();
                    std::string info = "[Инфо] Артист: " + current.artist + "\n"
                                     + "[Инфо] Название: " + current.title + "\n"
                                     + "[Инфо] ID: " + current.id + "\n"
                                     + "[Инфо] Обложка: " + (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) + "\n\n> ";
                    if (print) print(info);
                });
            } else if (m_cmdType == "quit") {
                if (ctx.onQuit) ctx.onQuit();
            } else if (m_cmdType == "help") {
                std::string s(50, '*');
                std::string helpText = "\n" + s + "\n [P] Play/Pause\n [N] Next\n [B] Prev\n [+] Vol Up\n [-] Vol Down\n [v <num>] Set Volume\n [seek <time>] Seek (e.g. seek 1:30 or seek 90)\n [st] Standard Order\n [sh] Shuffle\n [R] Repeat Mode\n [J <num>] Jump to track\n [cv] Current volume\n [rs] Reset Session\n [mode <0/1>] 0 - Standard, 1 - Gapless transition\n [savepos <0/1/2>] 0 - Off, 1 - Track only, 2 - Track + Position\n [search <text>] Search tracks in playlist\n [ly] Show lyrics for current track\n [logout / logout <service>] Logout and clear service cache\n [source] Select audio source\n [tl] Export tracklist to TXT\n [dl] / [dl <num>] Download track\n [rm] / [rm <num>] Delete downloaded track\n [pl <name>] Create playlist\n [pls] List playlists\n [pl play] Play playlist\n [pl rm <name>] Delete playlist\n [add] / [add <num>] Add track to playlist\n [drop <num>] Remove track from queue\n [vis] Toggle visualizer\n [Q] Quit\n" + s + "\n\n> ";
                if (ctx.print) ctx.print(helpText);
            }
        }
    };
}

void RegisterSystemCommands(std::map<std::string, std::unique_ptr<IConsoleCommand>>& commands) {
    commands["dl"] = std::make_unique<DownloadControlCommand>(false);
    commands["rm"] = std::make_unique<DownloadControlCommand>(true);
    commands["ly"] = std::make_unique<LyricsCommand>();
    commands["lyrics"] = std::make_unique<LyricsCommand>();
    commands["shazam"] = std::make_unique<ShazamCommand>();

    commands["source"] = std::make_unique<ConfigCommand>("source");
    commands["vis"] = std::make_unique<ConfigCommand>("vis");
    commands["mode"] = std::make_unique<ConfigCommand>("mode");
    commands["reload"] = std::make_unique<ConfigCommand>("reload");

    commands["logout"] = std::make_unique<SystemCommand>("logout");
    commands["i"] = std::make_unique<SystemCommand>("info");
    commands["q"] = std::make_unique<SystemCommand>("quit");
    commands["h"] = std::make_unique<SystemCommand>("help");
}
