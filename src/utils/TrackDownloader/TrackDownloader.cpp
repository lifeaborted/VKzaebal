#include "TrackDownloader.h"
#include "utils/logger/logger.h"
#include "bass.h"
#include <QDir>
#include <QFile>
#include <thread>

TrackDownloader::TrackDownloader(QObject* parent) : QObject(parent) {
    QDir().mkpath("downloads");
}

void TrackDownloader::Download(const Track& track, const std::string& urlStr) {
    QString trackId = QString::fromStdString(track.id);
    QString filePath = "downloads/" + trackId + ".wav";

    if (QFile::exists(filePath)) {
        Logger::Log(LogLevel::INFO, "[Загрузчик] Трек уже скачан: " + track.title);
        return;
    }

    Logger::Log(LogLevel::INFO, "[Загрузчик] Старт фонового декодирования (WAV): " + track.title);

    // Копируем данные для лямбды, чтобы они не уничтожились при выходе из функции
    std::string urlCopy = urlStr;
    Track trackCopy = track;

    std::thread([urlCopy, filePath, trackCopy]() {
        // Создаем декодирующий поток. Звук в динамики не идет.
        HSTREAM stream = BASS_StreamCreateURL(urlCopy.c_str(), 0, BASS_STREAM_DECODE, nullptr, nullptr);
        if (!stream) {
            Logger::Log(LogLevel::WARNING, "[Загрузчик] Ошибка потока BASS: " + std::to_string(BASS_ErrorGetCode()));
            return;
        }

        BASS_CHANNELINFO info;
        BASS_ChannelGetInfo(stream, &info);

        QFile file(filePath + ".tmp");
        if (!file.open(QIODevice::WriteOnly)) {
            BASS_StreamFree(stream);
            return;
        }

        // Оставляем пустые 44 байта под заголовок WAV-файла
        file.seek(44);

        BYTE buffer[40960]; // 40 КБ буфер
        uint32_t dataSize = 0;

        // Вытягиваем декодированные данные на максимальной скорости сети
        while (BASS_ChannelIsActive(stream) == BASS_ACTIVE_PLAYING) {
            DWORD bytesRead = BASS_ChannelGetData(stream, buffer, sizeof(buffer));
            if (bytesRead > 0 && bytesRead != (DWORD)-1) {
                file.write(reinterpret_cast<const char*>(buffer), bytesRead);
                dataSize += bytesRead;
            }
        }

        BASS_StreamFree(stream);

        // Возвращаемся в начало файла и генерируем правильный WAV заголовок
        file.seek(0);
        uint32_t chunkSize = 36 + dataSize;
        uint32_t sampleRate = info.freq;
        uint16_t numChannels = info.chans;
        uint16_t bitsPerSample = 16; // BASS по умолчанию декодирует в 16-бит
        uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
        uint16_t blockAlign = numChannels * (bitsPerSample / 8);
        uint16_t audioFormat = 1; // PCM формат

        file.write("RIFF", 4);
        file.write(reinterpret_cast<char*>(&chunkSize), 4);
        file.write("WAVE", 4);
        file.write("fmt ", 4);
        uint32_t fmtSize = 16;
        file.write(reinterpret_cast<char*>(&fmtSize), 4);
        file.write(reinterpret_cast<char*>(&audioFormat), 2);
        file.write(reinterpret_cast<char*>(&numChannels), 2);
        file.write(reinterpret_cast<char*>(&sampleRate), 4);
        file.write(reinterpret_cast<char*>(&byteRate), 4);
        file.write(reinterpret_cast<char*>(&blockAlign), 2);
        file.write(reinterpret_cast<char*>(&bitsPerSample), 2);
        file.write("data", 4);
        file.write(reinterpret_cast<char*>(&dataSize), 4);

        file.close();
        QFile::rename(filePath + ".tmp", filePath);

        Logger::Log(LogLevel::INFO, "[Загрузчик] Успешно сохранен WAV: " + trackCopy.artist + " - " + trackCopy.title);

    }).detach();
}