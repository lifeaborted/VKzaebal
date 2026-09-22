#include "TrackDownloader.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "services/network/NetworkStreamer.h"
#include "utils/parser/MpegTsDemuxer.h"
#include "utils/parser/Id3Utils.h"

#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <memory>
#include <algorithm>
#include <iostream>
#include <QCoreApplication>
#include <QPointer>

namespace {
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
        header.append(Id3Utils::MakeSyncSafe(static_cast<uint32_t>(tagData.size())));

        QFile srcFile(filePath);
        if (!srcFile.open(QIODevice::ReadOnly)) {
            Logger::Log(LogLevel::ERROR, "TrackDownloader: Failed to open source file for ID3 tagging: " + filePath.toStdString());
            return;
        }

        QString tempPath = filePath + ".tmp_id3";
        QFile dstFile(tempPath);
        if (!dstFile.open(QIODevice::WriteOnly)) {
            Logger::Log(LogLevel::ERROR, "TrackDownloader: Failed to create temp file for ID3 tagging: " + tempPath.toStdString());
            srcFile.close();
            return;
        }

        dstFile.write(header);
        dstFile.write(tagData);

        constexpr qint64 kChunkSize = 64 * 1024; // 64 KB streaming buffer
        char buffer[kChunkSize];
        bool readError = false;

        while (!srcFile.atEnd()) {
            qint64 bytesRead = srcFile.read(buffer, sizeof(buffer));
            if (bytesRead < 0) {
                readError = true;
                break;
            }
            if (bytesRead > 0) {
                dstFile.write(buffer, bytesRead);
            }
        }

        srcFile.close();
        dstFile.close();

        if (readError) {
            Logger::Log(LogLevel::ERROR, "TrackDownloader: Error while streaming audio data during ID3 tagging: " + filePath.toStdString());
            QFile::remove(tempPath);
            return;
        }

        if (!QFile::remove(filePath) || !QFile::rename(tempPath, filePath)) {
            Logger::Log(LogLevel::ERROR, "TrackDownloader: Failed to replace original file with tagged file: " + filePath.toStdString());
            QFile::remove(tempPath);
        }
    }
}

TrackDownloader::TrackDownloader(QObject* parent, QNetworkAccessManager* manager)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)) {
    QDir().mkpath(PathManager::GetDownloadsDir());
}

void TrackDownloader::Download(const Track& track, const std::string& urlStr, const QString& customDir) {
    std::string safeName = track.GetSafeFilename();

    QString filePath = PathManager::GetDownloadFilePath(safeName, "mp3", customDir);
    QString aacPath = PathManager::GetDownloadFilePath(safeName, "aac", customDir);
    auto syncPrint = [](const std::string& text) {
        Logger::Log(LogLevel::INFO, text);
    };

    if (QFile::exists(filePath) || QFile::exists(aacPath)) {
        Logger::Log(LogLevel::INFO, "[Загрузчик] Трек уже скачан: " + safeName);
        return;
    }

    QFileInfo fi(filePath);
    QDir().mkpath(fi.absolutePath());

    Logger::Log(LogLevel::INFO, "[Загрузчик] Старт загрузки (Universal Native): " + safeName);

    NetworkStreamer* streamer = new NetworkStreamer(this, m_manager);
    std::shared_ptr<QFile> file = std::make_shared<QFile>(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        Logger::Log(LogLevel::ERROR, "[Ошибка] Не удалось создать файл для сохранения: " + safeName);
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

    connect(streamer, &NetworkStreamer::DownloadError, this, [streamer, file, safeName](const std::string& err) {
        file->close();
        file->remove();
        streamer->deleteLater();
        Logger::Log(LogLevel::ERROR, "[Загрузчик] Ошибка скачивания " + safeName + ": " + err);
    });

    QPointer<TrackDownloader> safeThis(this);

    connect(streamer, &NetworkStreamer::DownloadFinished, this, [safeThis, streamer, file, track, filePath, safeName, isAacFormat, customDir, syncPrint]() {
        file->close();
        streamer->deleteLater();

        if (!safeThis) return;

        QString finalPath = filePath;
        if (*isAacFormat) {
            finalPath = PathManager::GetDownloadFilePath(safeName, "aac", customDir);
            QFile::rename(filePath, finalPath);
        }

        if (track.coverUrl.empty()) {
            InjectID3v2(finalPath, track, QByteArray());
            syncPrint("[Загрузка] " + track.artist + " - " + track.title + " успешно сохранен.");
            return;
        }

        QNetworkRequest request((QUrl(QString::fromStdString(track.coverUrl))));
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        QNetworkReply* reply = safeThis->m_manager->get(request);

        connect(reply, &QNetworkReply::finished, safeThis, [safeThis, reply, finalPath, track, syncPrint]() {
            reply->deleteLater();
            if (!safeThis) return;

            QByteArray coverData;
            if (reply->error() == QNetworkReply::NoError) {
                coverData = reply->readAll();
            }

            InjectID3v2(finalPath, track, coverData);
            syncPrint("[Загрузка] " + track.artist + " - " + track.title + " успешно сохранен.");
        });
    });

    streamer->StartDownload(urlStr);
}