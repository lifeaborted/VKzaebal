#include "TrackDownloader.h"
#include "utils/logger/Logger.h"
#include "bass.h"
#include "bassenc.h"
#include "bassenc_mp3.h"
#include <QDir>
#include <QFile>
#include <thread>
#include <QMetaObject>
#include <QNetworkRequest>
#include <QNetworkReply>

namespace {
    QByteArray makeSyncSafe(uint32_t size) {
        QByteArray b(4, 0);
        b[0] = (size >> 21) & 0x7F;
        b[1] = (size >> 14) & 0x7F;
        b[2] = (size >> 7) & 0x7F;
        b[3] = size & 0x7F;
        return b;
    }

    QByteArray makeUInt32BE(uint32_t size) {
        QByteArray b(4, 0);
        b[0] = (size >> 24) & 0xFF;
        b[1] = (size >> 16) & 0xFF;
        b[2] = (size >> 8) & 0xFF;
        b[3] = size & 0xFF;
        return b;
    }

    QByteArray createFrame(const QByteArray& id, const QByteArray& data) {
        QByteArray frame;
        frame.append(id.left(4));
        frame.append(makeUInt32BE(data.size()));
        frame.append('\0');
        frame.append('\0');
        frame.append(data);
        return frame;
    }

    // Фрейм с текстом (кодировка UTF-16)
    QByteArray createTextFrame(const QByteArray& id, const QString& text) {
        QByteArray data;
        data.append((char)0x01); // 0x01 = UTF-16 с BOM
        data.append((char)0xFF); // BOM
        data.append((char)0xFE);
        const ushort* utf16 = text.utf16();
        data.append(reinterpret_cast<const char*>(utf16), text.length() * 2);
        data.append('\0'); data.append('\0');
        return createFrame(id, data);
    }

    // Фрейм с картинкой (APIC)
    QByteArray createApicFrame(const QByteArray& imageData) {
        QByteArray data;
        data.append('\0');          // Кодировка текста (0 = ISO-8859-1)
        data.append("image/jpeg");  // MIME-тип
        data.append('\0');
        data.append((char)0x03);    // 0x03 = Лицевая обложка
        data.append('\0');          // Описание
        data.append(imageData);
        return createFrame("APIC", data);
    }

    // Главная функция: внедряет собранные теги в начало MP3-файла
    void InjectID3v2(const QString& filePath, const Track& track, const QByteArray& coverData) {
        QByteArray tagData;
        tagData.append(createTextFrame("TIT2", QString::fromStdString(track.title)));
        tagData.append(createTextFrame("TPE1", QString::fromStdString(track.artist)));
        if (!coverData.isEmpty()) {
            tagData.append(createApicFrame(coverData));
        }

        QByteArray header;
        header.append("ID3");
        header.append((char)0x03); // Версия ID3v2.3
        header.append('\0');       // Ревизия
        header.append('\0');       // Флаги
        header.append(makeSyncSafe(tagData.size())); // Размер тега

        QFile file(filePath);
        if (file.open(QIODevice::ReadWrite)) {
            QByteArray mp3Data = file.readAll();
            file.seek(0);
            file.write(header + tagData);
            file.write(mp3Data);
            file.close();
        }
    }
}

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

    std::thread([this, urlCopy, filePath, trackCopy, safeName]() {
        HSTREAM stream = BASS_StreamCreateURL(urlCopy.c_str(), 0, BASS_STREAM_DECODE, nullptr, nullptr);
        if (!stream) {
            Logger::Log(LogLevel::WARNING, "Ошибка потока BASS при скачивании.");
            return;
        }

        HENCODE encoder = BASS_Encode_MP3_StartFile(
            stream, "-b 320",
            BASS_ENCODE_AUTOFREE | BASS_UNICODE,
            reinterpret_cast<const char*>(filePath.utf16())
        );

        BYTE buffer[40960];
        while (BASS_ChannelIsActive(stream) == BASS_ACTIVE_PLAYING) {
            BASS_ChannelGetData(stream, buffer, sizeof(buffer));
        }

        BASS_StreamFree(stream);

        QMetaObject::invokeMethod(this, [this, filePath, trackCopy, safeName]() {
            if (trackCopy.coverUrl.empty()) {
                InjectID3v2(filePath, trackCopy, QByteArray());
                Logger::Log(LogLevel::INFO, "[Загрузчик] Сохранен MP3 (без обложки): " + safeName.toStdString());
                return;
            }

            QNetworkRequest request((QUrl(QString::fromStdString(trackCopy.coverUrl))));
            QNetworkReply* reply = m_manager.get(request);

            connect(reply, &QNetworkReply::finished, this, [reply, filePath, trackCopy, safeName]() {
                reply->deleteLater();
                QByteArray coverData;
                if (reply->error() == QNetworkReply::NoError) {
                    coverData = reply->readAll();
                }

                // Внедряем картинку и теги в MP3
                InjectID3v2(filePath, trackCopy, coverData);
                Logger::Log(LogLevel::INFO, "[Загрузчик] Сохранен MP3 с обложкой: " + safeName.toStdString());
            });
        }, Qt::QueuedConnection);

    }).detach();
}