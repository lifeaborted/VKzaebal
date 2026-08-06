#include "TrackDownloader.h"
#include "utils/logger/Logger.h"
#include "bass.h"
#include "bassenc.h"
#include "bassenc_mp3.h"
#include <QDir>
#include <QFile>
#include <thread>

TrackDownloader::TrackDownloader(QObject* parent) : QObject(parent) {
    QDir().mkpath("downloads");
}

void TrackDownloader::Download(const Track& track, const std::string& urlStr) {
    QString safeName = QString::fromStdString(track.GetSafeFilename());
    QString filePath = "downloads/" + safeName + ".mp3";

    if (QFile::exists(filePath)) {
        Logger::Log(LogLevel::INFO, "[Загрузчик] Трек уже скачан: " + safeName.toStdString());
        return;
    }

    Logger::Log(LogLevel::INFO, "[Загрузчик] Старт загрузки (MP3): " + safeName.toStdString());

    std::string urlCopy = urlStr;
    Track trackCopy = track;

    std::thread([urlCopy, filePath, trackCopy, safeName]() {
        HSTREAM stream = BASS_StreamCreateURL(urlCopy.c_str(), 0, BASS_STREAM_DECODE, nullptr, nullptr);
        if (!stream) {
            Logger::Log(LogLevel::WARNING, "Ошибка потока BASS при скачивании.");
            return;
        }

        // Запускаем MP3 энкодер
        HENCODE encoder = BASS_Encode_MP3_StartFile(stream, "-b 320", BASS_ENCODE_AUTOFREE, filePath.toStdString().c_str());

        BYTE buffer[40960];
        while (BASS_ChannelIsActive(stream) == BASS_ACTIVE_PLAYING) {
            BASS_ChannelGetData(stream, buffer, sizeof(buffer));
        }

        BASS_StreamFree(stream);
        Logger::Log(LogLevel::INFO, "[Загрузчик] Успешно сохранен MP3: " + safeName.toStdString());
    }).detach();
}