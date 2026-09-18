#include "CommandDispatcher.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "services/database/DatabaseManager.h"
#include "services/downloader/TrackDownloader.h"
#include "core/lyrics/LyricsFetcher.h"
#include "core/api/IAudioProvider.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "core/shazam/ShazamFFI.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <cmath>
#include <QSettings>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QtConcurrent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shobjidl.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comdlg32.lib")

#include <thread>
#include <chrono>
#include <cstdio>
#include <vector>

namespace {
    std::string OpenAudioFileDialog() {
        OPENFILENAMEW ofn;
        WCHAR szFile[MAX_PATH] = {0};

        ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"Audio Files\0*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&ofn) == TRUE) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, NULL, 0, NULL, NULL);
            if (size_needed > 0) {
                std::vector<char> buffer(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, ofn.lpstrFile, -1, &buffer[0], size_needed, NULL, NULL);
                return std::string(buffer.data());
            }
        }
        return "";
    }

#ifdef _WIN32
    std::string ChooseFolderNativeDialog() {
        std::string result;
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        IFileOpenDialog *pFileDialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileDialog)))) {
            DWORD dwOptions;
            if (SUCCEEDED(pFileDialog->GetOptions(&dwOptions))) {
                pFileDialog->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            }
            pFileDialog->SetTitle(L"Выберите папку для сохранения аудио");
            if (SUCCEEDED(pFileDialog->Show(NULL))) {
                IShellItem *pItem = nullptr;
                if (SUCCEEDED(pFileDialog->GetResult(&pItem))) {
                    PWSTR pszFilePath = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                        int size = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                        if (size > 0) {
                            std::vector<char> buffer(size);
                            WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, buffer.data(), size, NULL, NULL);
                            result = std::string(buffer.data());
                        }
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileDialog->Release();
        }
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
        return result;
    }
#endif

    void RunInMainThread(std::function<void()> func) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), func, Qt::QueuedConnection);
    }

    // КЛАССЫ КОМАНД
    class PlayPauseCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                if (ctx.audio.IsPlaying()) ctx.audio.Pause(); else ctx.audio.Resume();
            });
        }
    };

    class NextCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() { ctx.playlist.Next(); });
        }
    };

    class PrevCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() { ctx.playlist.Previous(); });
        }
    };

    class VolumeAdjustCommand : public IConsoleCommand {
        float m_delta;
    public:
        explicit VolumeAdjustCommand(float delta) : m_delta(delta) {}
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx, delta = m_delta]() { ctx.audio.SetVolume(ctx.audio.GetVolume() + delta); });
        }
    };

    class VolumeSetCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            try {
                int vol = std::stoi(arg);
                if (vol < 0) vol = 0;
                if (vol > 100) vol = 100;
                RunInMainThread([ctx, vol]() { ctx.audio.SetVolume(vol / 100.0f); });
                if (ctx.print) ctx.print("[Громкость] Установлена громкость: " + std::to_string(vol) + "%\n\n> ");
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: v <число от 0 до 100>\n\n> ");
            }
        }
    };

    class VolumeCurrentCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            int vol = static_cast<int>(std::round(ctx.audio.GetVolume() * 100));
            if (ctx.print) ctx.print("[Громкость] Текущая громкость: " + std::to_string(vol) + "%\n\n> ");
        }
    };

    class SeekCommand : public IConsoleCommand {
        static std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }

        static bool ParseDoubleStrict(const std::string& str, double& outVal) {
            std::string s = Trim(str);
            if (s.empty()) return false;
            try {
                size_t idx = 0;
                double val = std::stod(s, &idx);
                if (idx != s.length()) return false;
                outVal = val;
                return true;
            } catch (...) {
                return false;
            }
        }

        static bool ParseTime(const std::string& rawStr, double& outSeconds) {
            std::string str = Trim(rawStr);
            if (str.empty()) return false;

            size_t firstColon = str.find(':');
            if (firstColon != std::string::npos) {
                size_t secondColon = str.find(':', firstColon + 1);
                if (secondColon != std::string::npos) {
                    // hh:mm:ss
                    double hours = 0.0, mins = 0.0, secs = 0.0;
                    if (!ParseDoubleStrict(str.substr(0, firstColon), hours) ||
                        !ParseDoubleStrict(str.substr(firstColon + 1, secondColon - firstColon - 1), mins) ||
                        !ParseDoubleStrict(str.substr(secondColon + 1), secs)) {
                        return false;
                    }
                    if (hours < 0 || mins < 0 || mins >= 60 || secs < 0 || secs >= 60) return false;
                    outSeconds = hours * 3600.0 + mins * 60.0 + secs;
                    return true;
                } else {
                    // mm:ss or m:s
                    double mins = 0.0, secs = 0.0;
                    if (!ParseDoubleStrict(str.substr(0, firstColon), mins) ||
                        !ParseDoubleStrict(str.substr(firstColon + 1), secs)) {
                        return false;
                    }
                    if (mins < 0 || secs < 0 || secs >= 60) return false;
                    outSeconds = mins * 60.0 + secs;
                    return true;
                }
            } else {
                double val = 0.0;
                if (!ParseDoubleStrict(str, val) || val < 0) return false;
                outSeconds = val;
                return true;
            }
        }

        static std::string FormatTime(double seconds) {
            int totalSec = static_cast<int>(seconds);
            if (totalSec < 0) totalSec = 0;
            int hrs = totalSec / 3600;
            int mins = (totalSec % 3600) / 60;
            int secs = totalSec % 60;
            char buf[32];
            if (hrs > 0) {
                std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", hrs, mins, secs);
            } else {
                std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
            }
            return std::string(buf);
        }

        void Execute(const std::string& arg, CommandContext& ctx) override {
            double pos = 0.0;
            if (ParseTime(arg, pos)) {
                RunInMainThread([ctx, pos]() { ctx.audio.SetPositionSeconds(pos); });
                if (ctx.print) {
                    ctx.print("[Перемотка] Переход на " + FormatTime(pos) + " (" + std::to_string(static_cast<int>(pos)) + " сек.)\n\n> ");
                }
            } else {
                if (ctx.print) {
                    ctx.print("[Ошибка] Неверный формат времени. Используй: seek 1:30 (или seek 90)\n\n> ");
                }
            }
        }
    };

    class RepeatCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() { ctx.playlist.ToggleRepeat(); });
        }
    };

    class JumpCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            try {
                int idx = std::stoi(arg);
                RunInMainThread([ctx, idx]() { ctx.playlist.JumpToQueueIndex(idx - 1); });
                if (ctx.print) ctx.print("[Плейлист] Переход к треку " + std::to_string(idx) + "\n\n> ");
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
            }
        }
    };

    class ExportPlaylistCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Инфо] Текущий плейлист успешно экспортирован в playlist.txt\n\n> ");
        }
    };

    class ShuffleCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.playlist.SetShuffle(true);
                ctx.playlist.JumpToQueueIndex(0);
                std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
                ctx.dbManager.SaveQueue(ctx.playlist.GetQueueTracks(), src, ctx.playlist.IsShuffle());
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Плейлист] Режим: Перемешивание (Shuffle). Стартуем случайный трек!\n\n> ");
        }
    };

    class StandardOrderCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.playlist.SetShuffle(false);
                std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
                ctx.dbManager.SaveQueue(ctx.playlist.GetQueueTracks(), src, ctx.playlist.IsShuffle());
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
            });
            if (ctx.print) ctx.print("[Плейлист] Режим: Стандартный порядок\n\n> ");
        }
    };

    class ResetSessionCommand : public IConsoleCommand {
        void Execute(const std::string&, CommandContext& ctx) override {
            RunInMainThread([ctx]() {
                ctx.playlist.SetShuffle(false);
                ctx.playlist.JumpTo(0);
                std::string src = QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).value("General/source", "VK").toString().toStdString();
                ctx.dbManager.SaveQueue(ctx.playlist.GetQueueTracks(), src, ctx.playlist.IsShuffle());
                ctx.dbManager.ExportQueueToTxt(ctx.playlist.GetQueueTracks(), "playlist.txt", ctx.playlist.IsShuffle());
                ctx.dbManager.ClearSourceSession(src);
            });
            if (ctx.print) ctx.print("[Сессия] Плейлист сброшен: стандартный порядок, 1-й трек.\n\n> ");
        }
    };

    class SearchCommand : public IConsoleCommand {
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Пустой запрос. Используй: search <название или автор>\n\n> ");
                return;
            }

            std::string searchArtist = arg;
            std::string searchTitle = arg;
            bool isSplit = false;

            size_t dashPos = arg.find("-");
            if (dashPos != std::string::npos) {
                searchArtist = arg.substr(0, dashPos);
                searchTitle = arg.substr(dashPos + 1);
                auto trim = [](std::string& s) { s.erase(0, s.find_first_not_of(" \t")); s.erase(s.find_last_not_of(" \t") + 1); };
                trim(searchArtist);
                trim(searchTitle);
                isSplit = true;
            }

            QString qArtist = QString::fromStdString(searchArtist).trimmed();
            QString qTitle = QString::fromStdString(searchTitle).trimmed();
            QString qFull = QString::fromStdString(arg).trimmed();

            std::vector<Track> queue = ctx.playlist.GetQueueTracks();
            std::string s(50, '-');
            std::string res = "[Поиск] Результаты по запросу \"" + qFull.toStdString() + "\":\n" + s + "\n";

            int matchCount = 0;
            for (size_t i = 0; i < queue.size(); ++i) {
                QString trackArtist = QString::fromStdString(queue[i].artist);
                QString trackTitle = QString::fromStdString(queue[i].title);
                bool match = isSplit ? (trackArtist.contains(qArtist, Qt::CaseInsensitive) && trackTitle.contains(qTitle, Qt::CaseInsensitive))
                                     : (trackArtist.contains(qFull, Qt::CaseInsensitive) || trackTitle.contains(qFull, Qt::CaseInsensitive));
                if (match) {
                    res += "[" + std::to_string(i + 1) + "]. " + queue[i].artist + " - " + queue[i].title + " [" + queue[i].GetFormattedDuration() + "]\n";
                    matchCount++;
                    if (matchCount >= 20) {
                        res += "... Показаны первые 20 совпадений.\n";
                        break;
                    }
                }
            }

            if (matchCount == 0) res += "Ничего не найдено.\n";
            res += s + "\n\n> ";
            if (ctx.print) ctx.print(res);
        }
    };

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
                    if (idx >= 0 && idx < queue.size()) {
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
#ifdef _WIN32
                            std::string nativeFolder = ChooseFolderNativeDialog();
                            targetDir = QString::fromStdString(nativeFolder);
#else
                            targetDir = QFileDialog::getExistingDirectory(nullptr,
                                QString::fromUtf8("Выберите папку для сохранения аудио"),
                                QString(),
                                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
#endif

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

                        RunInMainThread([ctx, targetTrack, targetDir]() {
                            ctx.currentProvider->FetchTrackUrl(targetTrack.id, [ctx, targetTrack, targetDir](const std::string& url, bool err) {
                                if (!err && !url.empty()) ctx.downloader.Download(targetTrack, url, targetDir);
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
        void Execute(const std::string& arg, CommandContext& ctx) override {
            bool isNewFile = (arg.find("new") != std::string::npos);
            Track currentTrack = ctx.playlist.GetCurrentTrack();

            auto showLyricsFile = [ctx, currentTrack, isNewFile](const std::string& text) {
                if (text.empty()) {
                    if (ctx.print) ctx.print("[Ошибка] Не удалось загрузить текст (См. logs/app.log).\n\n> ");
                    return;
                }
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
                RunInMainThread([ctx, currentTrack, showLyricsFile]() {
                    ctx.lyricsFetcher.FetchLyrics(currentTrack.artist, currentTrack.title, [ctx, currentTrack, showLyricsFile](const std::string& fetchedText) {
                        if (!fetchedText.empty()) ctx.dbManager.UpdateTrackLyrics(currentTrack.id, fetchedText);
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
                filePath = OpenAudioFileDialog();
                if (filePath.empty()) {
                    if (ctx.print) ctx.print("[Shazam] Отмена. Файл не выбран.\n\n> ");
                    return;
                }
                if (ctx.print) ctx.print("[Shazam] Выбран файл: " + filePath + "\n> ");
            } else if (arg == "mic") {
                isMic = true;
                char tempPath[MAX_PATH];
                GetTempPathA(MAX_PATH, tempPath);
                filePath = std::string(tempPath) + "shazam_mic_record.wav";

                if (ctx.print) ctx.print("[Shazam] \xF0\x9F\x8E\xA4 Запись с микрофона (7 секунд)...\n> ");
            } else if (!arg.empty()) {
                filePath = arg;
            } else {
                if (ctx.print) ctx.print("\n=== Выбор источника ===\n1 - ВКонтакте\n2 - Spotify\n3 - SoundCloud\n4 - Yandex\n5 - YouTube Music\n6 - Оффлайн режим\n\nВведите номер: ");
                return;
            }

            QThreadPool::globalInstance()->start([ctx, filePath, isMic]() {
                if (isMic) {
                    mciSendStringA("open new type waveaudio alias rec", NULL, 0, NULL);
                    mciSendStringA("record rec", NULL, 0, NULL);
                    std::this_thread::sleep_for(std::chrono::seconds(7));

                    std::string saveCmd = "save rec \"" + filePath + "\"";
                    mciSendStringA(saveCmd.c_str(), NULL, 0, NULL);
                    mciSendStringA("close rec", NULL, 0, NULL);
                }

                RunInMainThread([ctx]() {
                    if (ctx.print) ctx.print("\n[Shazam] Анализ аудио...\n> ");
                });

                char* raw_base64 = generate_shazam_signature(filePath.c_str());

                if (isMic) {
                    std::remove(filePath.c_str());
                }

                if (!raw_base64) {
                    RunInMainThread([ctx]() {
                        if (ctx.print) ctx.print("\n[Shazam] Ошибка: Не удалось обработать аудио.\n> ");
                    });
                    return;
                }

                QString base64Sig = QString::fromUtf8(raw_base64);
                free_shazam_string(raw_base64);

                RunInMainThread([ctx, base64Sig]() {
                    QJsonObject sigObj{ {"uri", base64Sig}, {"samplems", 12000} };
                    QJsonObject rootObj{ {"signature", sigObj} };
                    QByteArray jsonPayload = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);

                    QNetworkAccessManager* manager = new QNetworkAccessManager();
                    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    QUrl url("https://amp.shazam.com/discovery/v5/ru/RU/android/-/tag/" + uuid + "/" + uuid);

                    QNetworkRequest request(url);
                    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                    request.setRawHeader("User-Agent", "Shazam Android/13.7.0");

                    QNetworkReply* reply = manager->post(request, jsonPayload);

                    QObject::connect(reply, &QNetworkReply::finished, [ctx, reply, manager]() {
                        if (reply->error() == QNetworkReply::NoError) {
                            QJsonObject trackObj = QJsonDocument::fromJson(reply->readAll()).object()["track"].toObject();
                            if (trackObj.isEmpty()) {
                                if (ctx.print) ctx.print("\n[Shazam] Трек не распознан :( Возможно, его нет в базе.\n> ");
                            } else {
                                QString title = trackObj["title"].toString();
                                QString artist = trackObj["subtitle"].toString();
                                if (ctx.print) ctx.print("\n[Shazam] Найдено: " + artist.toStdString() + " - " + title.toStdString() + "\n> ");
                            }
                        } else {
                            if (ctx.print) ctx.print("\n[Shazam] Ошибка сети: " + reply->errorString().toStdString() + "\n> ");
                        }
                        reply->deleteLater();
                        manager->deleteLater();
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
                if (ctx.print) ctx.print("=== Выбор источника ===\n\n  [1] ВКонтакте\n  [2] Spotify\n  [3] SoundCloud\n  [4] Yandex\n  [5] YouTube\n  [6] Оффлайн режим\n  [7] Общий микс (Все сервисы)\n  [8] Плейлисты\n\n  [0] Отмена\n\nВыберите номер: ");
                if (ctx.onSourceChange) ctx.onSourceChange("SELECT");
            } else if (m_cmdType == "vis") {
                if (ctx.onVisualizerToggle) ctx.onVisualizerToggle();
            } else if (m_cmdType == "mode") {
                try {
                    int mode = std::stoi(arg);
                    if (mode == 0 || mode == 1) {
                        bool isGapless = (mode == 1);
                        if (ctx.onGaplessMode) RunInMainThread([ctx, isGapless]() { ctx.onGaplessMode(isGapless); });
                        if (ctx.print) ctx.print("[Режим] Установлен " + std::string(isGapless ? "плавный (gapless)" : "стандартный") + " переход.\n\n> ");
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Используй: mode 0 (стандарт) или mode 1 (плавный)\n\n> ");
                    }
                } catch (...) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: mode 0 или mode 1\n\n> ");
                }
            } else if (m_cmdType == "reload") {
                if (ctx.onReloadUi) RunInMainThread([ctx]() { ctx.onReloadUi(); });
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
                    if (ctx.onLogout) RunInMainThread([ctx, arg]() { ctx.onLogout(arg); });
                } else if (arg.empty()) {
                    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
                    QString currentSrc = settings.value("General/source", "").toString().toLower();
                    if (currentSrc == "vk" || currentSrc == "spotify" || currentSrc == "sc" || currentSrc == "soundcloud" || currentSrc == "yandex" || currentSrc == "youtube") {
                        if (ctx.onLogout) RunInMainThread([ctx, currentSrc]() { ctx.onLogout(currentSrc.toStdString()); });
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Укажите сервис: logout vk | logout spotify | logout sc | logout yandex | logout youtube | logout all\n\n> ");
                    }
                } else {
                    if (ctx.print) ctx.print("[Ошибка] Укажите сервис: logout vk | logout spotify | logout sc | logout yandex | logout youtube | logout all\n\n> ");
                }
            } else if (m_cmdType == "info") {
                RunInMainThread([ctx]() {
                    Track current = ctx.playlist.GetCurrentTrack();
                    std::string info = "[Инфо] Артист: " + current.artist + "\n"
                                     + "[Инфо] Название: " + current.title + "\n"
                                     + "[Инфо] ID: " + current.id + "\n"
                                     + "[Инфо] Обложка: " + (current.coverUrl.empty() ? "НЕТ ОБЛОЖКИ" : current.coverUrl) + "\n\n> ";
                    if (ctx.print) ctx.print(info);
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

    class PlaylistControlCommand : public IConsoleCommand {
        static std::string Trim(const std::string& str) {
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return "";
            size_t end = str.find_last_not_of(" \t\r\n");
            return str.substr(start, end - start + 1);
        }

    public:
        void Execute(const std::string& rawArg, CommandContext& ctx) override {
            std::string arg = Trim(rawArg);
            if (arg.empty() || arg == "help") {
                std::string help = "\n[Плейлисты] Команды управления плейлистами:\n"
                                   "  pl <название>       - Создать новый плейлист\n"
                                   "  pl play             - Выбрать плейлист для воспроизведения\n"
                                   "  pl play <название>  - Запустить плейлист с указанным названием\n"
                                   "  pl rm <название>    - Удалить плейлист\n"
                                   "  pls                 - Список всех плейлистов\n"
                                   "  add                 - Добавить текущий трек в плейлист\n"
                                   "  add <номер>         - Добавить трек из очереди в плейлист\n"
                                   "  drop <номер>        - Удалить трек из текущей очереди\n\n> ";
                if (ctx.print) ctx.print(help);
                return;
            }

            if (arg == "play") {
                if (ctx.onSelectPlaylistToPlay) {
                    RunInMainThread([ctx]() { ctx.onSelectPlaylistToPlay(); });
                }
                return;
            }

            if (arg.rfind("play ", 0) == 0) {
                std::string plName = Trim(arg.substr(5));
                if (plName.empty()) {
                    if (ctx.onSelectPlaylistToPlay) {
                        RunInMainThread([ctx]() { ctx.onSelectPlaylistToPlay(); });
                    }
                    return;
                }
                auto pls = ctx.dbManager.GetPlaylists();
                bool found = false;
                for (const auto& p : pls) {
                    if (p.name == plName) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    if (ctx.onSourceChange) {
                        RunInMainThread([ctx, plName]() { ctx.onSourceChange("Custom:" + plName); });
                    }
                } else {
                    if (ctx.print) ctx.print("[Ошибка] Плейлист '" + plName + "' не найден.\n\n> ");
                }
                return;
            }

            if (arg.rfind("rm ", 0) == 0) {
                std::string plName = Trim(arg.substr(3));
                if (plName.empty()) {
                    if (ctx.print) ctx.print("[Ошибка] Укажите название плейлиста для удаления. Например: pl rm Мой Плейлист\n\n> ");
                    return;
                }
                bool ok = ctx.dbManager.DeletePlaylist(plName);
                if (ctx.print) {
                    ctx.print(ok ? ("[Плейлисты] Плейлист '" + plName + "' успешно удален.\n\n> ")
                                 : ("[Ошибка] Плейлист '" + plName + "' не найден.\n\n> "));
                }
                return;
            }

            // Создание плейлиста
            bool ok = ctx.dbManager.CreatePlaylist(arg);
            if (ctx.print) {
                ctx.print(ok ? ("[Плейлисты] Плейлист '" + arg + "' успешно создан!\n\n> ")
                             : ("[Ошибка] Плейлист с именем '" + arg + "' уже существует или ошибка создания.\n\n> "));
            }
        }
    };

    class PlaylistListCommand : public IConsoleCommand {
    public:
        void Execute(const std::string&, CommandContext& ctx) override {
            auto playlists = ctx.dbManager.GetPlaylists();
            if (playlists.empty()) {
                if (ctx.print) ctx.print("[Плейлисты] Нет сохраненных плейлистов. Создай через: pl <название>\n\n> ");
                return;
            }

            std::string s(50, '=');
            std::string res = "\n" + s + "\n=== Ваши плейлисты ===\n";
            for (size_t i = 0; i < playlists.size(); ++i) {
                res += " [" + std::to_string(i + 1) + "] " + playlists[i].name
                     + " (" + std::to_string(playlists[i].trackCount) + " треков)\n";
            }
            res += s + "\nИспользуй 'pl play' для запуска или 'pl <название>' для создания нового.\n\n> ";
            if (ctx.print) ctx.print(res);
        }
    };

    class AddTrackToPlaylistCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& arg, CommandContext& ctx) override {
            Track targetTrack;
            if (arg.empty()) {
                targetTrack = ctx.playlist.GetCurrentTrack();
                if (targetTrack.id.empty()) {
                    if (ctx.print) ctx.print("[Ошибка] Сейчас никакой трек не играет.\n\n> ");
                    return;
                }
            } else {
                try {
                    int num = std::stoi(arg);
                    int idx = num - 1;
                    std::vector<Track> queue = ctx.playlist.GetQueueTracks();
                    if (idx >= 0 && idx < static_cast<int>(queue.size())) {
                        targetTrack = queue[idx];
                    } else {
                        if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                        return;
                    }
                } catch (...) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: add или add <номер>\n\n> ");
                    return;
                }
            }

            if (!targetTrack.id.empty() && ctx.onSelectPlaylist) {
                RunInMainThread([ctx, targetTrack]() {
                    ctx.onSelectPlaylist(targetTrack);
                });
            }
        }
    };

    class DropTrackFromPlaylistCommand : public IConsoleCommand {
    public:
        void Execute(const std::string& arg, CommandContext& ctx) override {
            if (arg.empty()) {
                if (ctx.print) ctx.print("[Ошибка] Используй: drop <номер трека в очереди>\n\n> ");
                return;
            }

            try {
                int num = std::stoi(arg);
                int idx = num - 1;
                std::vector<Track> queue = ctx.playlist.GetQueueTracks();
                if (idx < 0 || idx >= static_cast<int>(queue.size())) {
                    if (ctx.print) ctx.print("[Ошибка] Неверный номер трека.\n\n> ");
                    return;
                }

                Track droppedTrack = queue[idx];

                RunInMainThread([ctx, droppedTrack, num]() {
                    auto allTracks = ctx.playlist.GetAllTracks();
                    int absIndex = -1;
                    for (size_t i = 0; i < allTracks.size(); ++i) {
                        if (allTracks[i].id == droppedTrack.id) {
                            absIndex = static_cast<int>(i);
                            break;
                        }
                    }

                    // Если сейчас играет кастомный плейлист, удаляем и из БД
                    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
                    std::string activeSource = settings.value("General/source", "").toString().toStdString();
                    if (activeSource.rfind("Custom:", 0) == 0 && absIndex >= 0) {
                        std::string plName = activeSource.substr(7);
                        int plId = -1;
                        ctx.dbManager.LoadPlaylistTracksByName(plName, plId);
                        if (plId > 0) {
                            ctx.dbManager.RemoveTrackFromPlaylist(plId, absIndex);
                        }
                    }

                    if (absIndex >= 0) {
                        ctx.playlist.RemoveTrack(absIndex);
                    }

                    if (ctx.print) {
                        ctx.print("[Очередь] Трек #" + std::to_string(num) + " (" + droppedTrack.artist + " - " + droppedTrack.title + ") удален.\n\n> ");
                    }
                });
            } catch (...) {
                if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: drop <номер трека>\n\n> ");
            }
        }
    };
}

CommandDispatcher::CommandDispatcher(IAudioEngine& audio, PlaylistManager& playlist, DatabaseManager& dbManager, TrackDownloader& downloader, LyricsFetcher& lyricsFetcher)
    : m_audio(audio), m_playlist(playlist), m_dbManager(dbManager), m_downloader(downloader), m_lyricsFetcher(lyricsFetcher) {
    RegisterCommands();
}

CommandDispatcher::~CommandDispatcher() = default;

void CommandDispatcher::SetCurrentProvider(IAudioProvider* provider) { m_currentProvider = provider; }
void CommandDispatcher::SetPrintCallback(std::function<void(const std::string&)> printCb) { m_printCb = printCb; }
void CommandDispatcher::Print(const std::string& msg) { if (m_printCb) m_printCb(msg); }

void CommandDispatcher::Dispatch(const std::string& input) {
    if (input.empty()) return;

    std::string cmd;
    std::string arg;
    size_t spacePos = input.find(' ');

    if (spacePos != std::string::npos) {
        cmd = input.substr(0, spacePos);
        arg = input.substr(spacePos + 1);
    } else {
        cmd = input;
    }
    for (char& c : cmd) c = std::tolower(c);

    auto it = m_commands.find(cmd);
    if (it != m_commands.end()) {
        CommandContext ctx {
            m_audio, m_playlist, m_dbManager, m_downloader, m_lyricsFetcher, m_currentProvider,
            m_printCb, OnSourceChangeRequested, OnGaplessModeChanged, OnVisualizerToggled,
            OnQuitRequested, OnLogoutRequested, OnReloadUiRequested,
            OnSelectPlaylistRequested, OnSelectPlaylistToPlayRequested
        };
        it->second->Execute(arg, ctx);
    } else {
        Print("[Ошибка] Неизвестная команда. Введи 'h' для справки.\n\n> ");
    }
}

class SavePosCommand : public IConsoleCommand {
    void Execute(const std::string& arg, CommandContext& ctx) override {
        try {
            int mode = std::stoi(arg);
            if (mode >= 0 && mode <= 2) {
                QSettings(PathManager::GetConfigPath(), QSettings::IniFormat).setValue("Playback/SavePosition", mode);
                std::string msg;
                if (mode == 0) {
                    msg = "[Режим] Сохранение позиции отключено (всегда сначала).\n\n> ";
                } else if (mode == 1) {
                    msg = "[Режим] Теперь плеер запоминает только трек (время с 0:00).\n\n> ";
                } else {
                    msg = "[Режим] Теперь плеер запоминает трек и точную позицию по времени.\n\n> ";
                }
                if (ctx.print) ctx.print(msg);
            } else {
                if (ctx.print) ctx.print("[Ошибка] Используй: savepos 0 (выкл), savepos 1 (только трек), savepos 2 (трек + время)\n\n> ");
            }
        } catch (...) {
            if (ctx.print) ctx.print("[Ошибка] Неверный формат. Используй: savepos 0, 1 или 2\n\n> ");
        }
    }
};

void CommandDispatcher::RegisterCommands() {
    m_commands["p"] = std::make_unique<PlayPauseCommand>();
    m_commands["n"] = std::make_unique<NextCommand>();
    m_commands["b"] = std::make_unique<PrevCommand>();
    m_commands["+"] = std::make_unique<VolumeAdjustCommand>(0.1f);
    m_commands["-"] = std::make_unique<VolumeAdjustCommand>(-0.1f);
    m_commands["v"] = std::make_unique<VolumeSetCommand>();
    m_commands["cv"] = std::make_unique<VolumeCurrentCommand>();
    m_commands["seek"] = std::make_unique<SeekCommand>();
    m_commands["r"] = std::make_unique<RepeatCommand>();
    m_commands["j"] = std::make_unique<JumpCommand>();
    m_commands["tl"] = std::make_unique<ExportPlaylistCommand>();
    m_commands["sh"] = std::make_unique<ShuffleCommand>();
    m_commands["st"] = std::make_unique<StandardOrderCommand>();
    m_commands["rs"] = std::make_unique<ResetSessionCommand>();
    m_commands["reset"] = std::make_unique<ResetSessionCommand>();
    m_commands["search"] = std::make_unique<SearchCommand>();
    m_commands["dl"] = std::make_unique<DownloadControlCommand>(false);
    m_commands["rm"] = std::make_unique<DownloadControlCommand>(true);
    m_commands["ly"] = std::make_unique<LyricsCommand>();
    m_commands["lyrics"] = std::make_unique<LyricsCommand>();
    m_commands["shazam"] = std::make_unique<ShazamCommand>();

    m_commands["pl"] = std::make_unique<PlaylistControlCommand>();
    m_commands["pls"] = std::make_unique<PlaylistListCommand>();
    m_commands["add"] = std::make_unique<AddTrackToPlaylistCommand>();
    m_commands["drop"] = std::make_unique<DropTrackFromPlaylistCommand>();

    m_commands["source"] = std::make_unique<ConfigCommand>("source");
    m_commands["vis"] = std::make_unique<ConfigCommand>("vis");
    m_commands["mode"] = std::make_unique<ConfigCommand>("mode");
    m_commands["reload"] = std::make_unique<ConfigCommand>("reload");
    m_commands["savepos"] = std::make_unique<SavePosCommand>();

    m_commands["logout"] = std::make_unique<SystemCommand>("logout");
    m_commands["i"] = std::make_unique<SystemCommand>("info");
    m_commands["q"] = std::make_unique<SystemCommand>("quit");
    m_commands["h"] = std::make_unique<SystemCommand>("help");
}