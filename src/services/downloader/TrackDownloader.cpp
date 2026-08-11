#include "TrackDownloader.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"
#include "services/network/NetworkStreamer.h"

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

    auto tsBuffer = std::make_shared<std::vector<uint8_t>>();
    auto audioPid = std::make_shared<uint16_t>(0x1FFF);
    auto id3BytesToSkip = std::make_shared<size_t>(0);
    auto isAacFormat = std::make_shared<bool>(false);
    auto formatDetected = std::make_shared<bool>(false);

    connect(streamer, &NetworkStreamer::DataReceived, [file, tsBuffer, audioPid, id3BytesToSkip, isAacFormat, formatDetected](const QByteArray& data) {
        // Проверка: если сервис отдал прямой файл без контейнера MPEG-TS (нет 0x47)
        if (tsBuffer->empty() && data.size() > 0 && static_cast<uint8_t>(data[0]) != 0x47) {
            file->write(data);
            return;
        }

        tsBuffer->insert(tsBuffer->end(), data.begin(), data.end());
        size_t bytesConsumed = 0;

        while (tsBuffer->size() - bytesConsumed >= 188) {
            const uint8_t* tsPacket = tsBuffer->data() + bytesConsumed;

            if (tsPacket[0] != 0x47) {
                auto startIt = tsBuffer->begin() + bytesConsumed;
                auto it = std::find(startIt, tsBuffer->end(), 0x47);
                bytesConsumed = std::distance(tsBuffer->begin(), it);
                continue;
            }

            uint16_t pid = ((tsPacket[1] & 0x1F) << 8) | tsPacket[2];
            uint8_t pusi = (tsPacket[1] & 0x40) >> 6;
            uint8_t afc  = (tsPacket[3] & 0x30) >> 4;

            size_t payloadOffset = 4;
            if (afc == 2 || afc == 3) payloadOffset += 1 + tsPacket[4];

            if ((afc == 1 || afc == 3) && payloadOffset < 188) {
                size_t payloadSize = 188 - payloadOffset;
                const uint8_t* payload = tsPacket + payloadOffset;

                if (pusi == 1 && payloadSize >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                    uint8_t streamId = payload[3];

                    if (*audioPid == 0x1FFF && streamId >= 0xC0 && streamId <= 0xDF) {
                        *audioPid = pid;
                    }

                    if (pid == *audioPid) {
                        uint8_t pesHeaderLen = payload[8];
                        size_t pesTotalOffset = 9 + pesHeaderLen;
                        if (pesTotalOffset < payloadSize) {
                            payload += pesTotalOffset;
                            payloadSize -= pesTotalOffset;
                        } else {
                            payloadSize = 0;
                        }

                        if (payloadSize >= 10 && payload[0] == 'I' && payload[1] == 'D' && payload[2] == '3') {
                            uint32_t tagSize = ((payload[6] & 0x7F) << 21) | ((payload[7] & 0x7F) << 14) |
                                               ((payload[8] & 0x7F) << 7)  | (payload[9] & 0x7F);
                            *id3BytesToSkip = 10 + tagSize;
                        }
                    }
                }

                if (pid == *audioPid && *id3BytesToSkip > 0 && payloadSize > 0) {
                    size_t toSkip = std::min(*id3BytesToSkip, payloadSize);
                    payload += toSkip;
                    payloadSize -= toSkip;
                    *id3BytesToSkip -= toSkip;
                }

                if (payloadSize > 0 && pid == *audioPid) {
                    // ОПРЕДЕЛЕНИЕ КОДЕКА (AAC vs MP3)
                    if (!(*formatDetected) && payloadSize >= 2) {
                        if (payload[0] == 0xFF && ((payload[1] & 0xF0) == 0xF0) && (((payload[1] >> 1) & 0x03) == 0x00)) {
                            *isAacFormat = true; // Это AAC!
                        }
                        *formatDetected = true;
                    }

                    file->write(reinterpret_cast<const char*>(payload), payloadSize);
                }
            }
            bytesConsumed += 188;
        }

        if (bytesConsumed > 0) {
            tsBuffer->erase(tsBuffer->begin(), tsBuffer->begin() + bytesConsumed);
        }
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