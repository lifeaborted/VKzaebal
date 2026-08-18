#include "TrackDownloader.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "services/network/NetworkStreamer.h"
#include "utils/parser/MpegTsDemuxer.h"

#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <memory>
#include <algorithm>
#include <iostream>
#include <QCoreApplication>

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

    QByteArray createTextFrame(const QByteArray& id, const QString& text) {
        QByteArray data;
        data.append((char)0x01);
        data.append((char)0xFF);
        data.append((char)0xFE);
        const ushort* utf16 = text.utf16();
        data.append(reinterpret_cast<const char*>(utf16), text.length() * 2);
        data.append('\0'); data.append('\0');
        return createFrame(id, data);
    }

    QByteArray createApicFrame(const QByteArray& imageData) {
        QByteArray data;
        data.append('\0');
        data.append("image/jpeg");
        data.append('\0');
        data.append((char)0x03);
        data.append('\0');
        data.append(imageData);
        return createFrame("APIC", data);
    }

    void InjectID3v2(const QString& filePath, const Track& track, const QByteArray& coverData) {
        QByteArray tagData;
        tagData.append(createTextFrame("TIT2", QString::fromStdString(track.title)));
        tagData.append(createTextFrame("TPE1", QString::fromStdString(track.artist)));
        if (!coverData.isEmpty()) {
            tagData.append(createApicFrame(coverData));
        }

        QByteArray header;
        header.append("ID3");
        header.append((char)0x03);
        header.append('\0');
        header.append('\0');
        header.append(makeSyncSafe(tagData.size()));

        QFile file(filePath);
        if (file.open(QIODevice::ReadWrite)) {
            QByteArray rawAudioData = file.readAll();
            file.seek(0);
            file.write(header + tagData);
            file.write(rawAudioData);
            file.close();
        }
    }
}

TrackDownloader::TrackDownloader(QObject* parent) : QObject(parent) {
    QDir().mkpath(PathManager::GetDownloadsDir());
}

void TrackDownloader::Download(const Track& track, const std::string& urlStr) {
    std::string safeName = track.GetSafeFilename();

    QString filePath = PathManager::GetDownloadFilePath(safeName, "mp3");
    QString aacPath = PathManager::GetDownloadFilePath(safeName, "aac");
    auto syncPrint = [](const std::string& text) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [text]() {
            std::lock_guard<std::mutex> lock(Logger::GetMutex());
            std::cout << "\r\033[2K\033[1A\r\033[2K" << text << "\n\n> ";
            std::cout.flush();
        }, Qt::QueuedConnection);
    };

    if (QFile::exists(filePath) || QFile::exists(aacPath)) {
        Logger::Log(LogLevel::INFO, "[Загрузчик] Трек уже скачан: " + safeName);
        return;
    }

    Logger::Log(LogLevel::INFO, "[Загрузчик] Старт загрузки (Universal Native): " + safeName);

    NetworkStreamer* streamer = new NetworkStreamer(this);
    std::shared_ptr<QFile> file = std::make_shared<QFile>(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        streamer->deleteLater();
        return;
    }

    auto isAacFormat = std::make_shared<bool>(false);

    // Вся та логика схлопнулась вот в этот элегантный объект!
    auto demuxer = std::make_shared<MpegTsDemuxer>([file, isAacFormat](const uint8_t* payload, size_t size, AudioFormat format) {
        if (format == AudioFormat::AAC_ADTS) {
            *isAacFormat = true;
        }
        file->write(reinterpret_cast<const char*>(payload), size);
    });

    connect(streamer, &NetworkStreamer::DataReceived, [demuxer](const QByteArray& data) {
        demuxer->ProcessBytes(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    });

    connect(streamer, &NetworkStreamer::DownloadFinished, this, [this, streamer, file, track, filePath, safeName, isAacFormat, syncPrint]() {
        file->close();
        streamer->deleteLater();

        QString finalPath = filePath;
        if (*isAacFormat) {
            finalPath = PathManager::GetDownloadFilePath(safeName, "aac");
            QFile::rename(filePath, finalPath);
        }

        if (track.coverUrl.empty()) {
            InjectID3v2(finalPath, track, QByteArray());
            syncPrint("[Загрузка] " + track.artist + " - " + track.title + " успешно сохранен (без обложки).");
            return;
        }

        QNetworkRequest request((QUrl(QString::fromStdString(track.coverUrl))));
        QNetworkReply* reply = m_manager.get(request);

        connect(reply, &QNetworkReply::finished, this, [reply, finalPath, track, syncPrint]() {
            QByteArray coverData;
            if (reply->error() == QNetworkReply::NoError) {
                coverData = reply->readAll();
            }
            reply->deleteLater();
            InjectID3v2(finalPath, track, coverData);

            syncPrint("[Загрузка] " + track.artist + " - " + track.title + " успешно сохранен.");
        });
    });

    streamer->StartDownload(urlStr);
}