#include "NetworkStreamer.h"
#include "utils/logger/Logger.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QThreadPool>
#include <QPointer>
#include <QCoreApplication>

NetworkStreamer::NetworkStreamer(QObject* parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)), m_reply(nullptr) {
    Logger::Log(LogLevel::INFO, "NetworkStreamer created.");
}

NetworkStreamer::~NetworkStreamer() {
    StopDownload();
    Logger::Log(LogLevel::INFO, "NetworkStreamer destroyed.");
}

void NetworkStreamer::StartDownload(const std::string& urlString) {
    Logger::Log(LogLevel::INFO, "Starting network stream from: " + urlString);
    m_pendingSeekPos = -1.0;
    m_totalFileSize = 0;

    if (m_reply) {
        StopDownload();
    }
    m_streamType = StreamType::DirectHttp;
    m_baseUrl = QUrl(QString::fromStdString(urlString));

    m_chunkQueue.clear();
    m_isEncrypted = false;
    m_mediaSequence = 0;
    m_aesKey.clear();
    m_aesIV.clear();
    m_currentChunkData.clear();

    QUrl url(QString::fromStdString(urlString));
    QNetworkRequest request(url);
    m_baseUrl = url;

    // Включаем поддержку редиректов
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);
    m_isPaused = false;

    if (urlString.find(".m3u8") != std::string::npos) {
        connect(m_reply, &QNetworkReply::finished, this, [this, url]() {
            if (m_reply && m_reply->error() == QNetworkReply::NoError) {
                QString manifest = m_reply->readAll();
                ParseM3u8(manifest, url);

                m_reply->deleteLater();
                m_reply = nullptr;

                if (m_isEncrypted) {
                    DownloadKey();
                } else {
                    DownloadNextChunk();
                }
            } else {
                Logger::Log(LogLevel::ERROR, "Failed to download .m3u8 manifest");
                emit DownloadError("Failed to download .m3u8 manifest");
                if (m_reply) {
                    m_reply->deleteLater();
                    m_reply = nullptr;
                }
            }
        });
        connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
    }
    else {
        connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
        connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
        connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
    }
}

void NetworkStreamer::StopDownload() {
    m_chunkQueue.clear();
    if (m_reply) {
        Logger::Log(LogLevel::INFO, "Aborting network stream.");
        
        m_reply->disconnect();

        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void NetworkStreamer::ParseM3u8(const QString& manifestData, const QUrl& baseUrl) {
    QStringList lines = manifestData.split('\n');

    QRegularExpression keyRegex("#EXT-X-KEY:METHOD=AES-128,URI=\"([^\"]+)\"(?:,IV=(?:0x)?([0-9a-fA-F]+))?");
    QRegularExpression seqRegex("#EXT-X-MEDIA-SEQUENCE:(\\d+)");

    QRegularExpressionMatch seqMatch = seqRegex.match(manifestData);
    if (seqMatch.hasMatch()) {
        m_baseMediaSequence = seqMatch.captured(1).toULongLong();
        m_mediaSequence = m_baseMediaSequence; // Синхронизируем
    }

    QUrlQuery baseQuery(baseUrl);
    m_hlsChunks.clear();
    double currentDuration = 0.0;

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();

        QRegularExpressionMatch match = keyRegex.match(trimmed);
        if (match.hasMatch()) {
            m_isEncrypted = true;
            QUrl keyUrlResolved = baseUrl.resolved(QUrl(match.captured(1)));
            // ... [слияние параметров ключа оставляем как есть] ...
            m_keyUrl = keyUrlResolved;
            if (!match.captured(2).isEmpty()) {
                m_aesIV = QByteArray::fromHex(match.captured(2).toUtf8());
            }
            continue;
        }

        // ДОБАВЛЕНО: Парсим длительность чанка
        if (trimmed.startsWith("#EXTINF:")) {
            QString valStr = trimmed.mid(8);
            int commaPos = valStr.indexOf(',');
            if (commaPos != -1) valStr = valStr.left(commaPos);
            currentDuration = valStr.toDouble();
            continue;
        }

        if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;

        QUrl chunkUrl = baseUrl.resolved(QUrl(trimmed));

        // слияние параметров запроса для чанка
        QUrlQuery chunkQuery(chunkUrl);
        for (const auto& item : baseQuery.queryItems()) {
            if (!chunkQuery.hasQueryItem(item.first)) {
                chunkQuery.addQueryItem(item.first, item.second);
            }
        }

        chunkUrl.setQuery(chunkQuery);

        m_hlsChunks.push_back({chunkUrl, currentDuration});
        m_chunkQueue.enqueue(chunkUrl);
    }

    // ДОБАВЛЕНО: Умное определение типа потока
    if (m_hlsChunks.size() == 1 && m_isEncrypted) {
        m_streamType = StreamType::EncryptedMonolith;
    } else if (m_isEncrypted) {
        m_streamType = StreamType::HlsEncrypted;
    } else {
        m_streamType = StreamType::HlsUnencrypted;
    }

    Logger::Log(LogLevel::INFO, "Parsed " + std::to_string(m_hlsChunks.size()) + " audio chunks. Encrypted: " + (m_isEncrypted ? "Yes" : "No"));
    if (m_pendingSeekPos >= 0.0) {
        double pos = m_pendingSeekPos;
        m_pendingSeekPos = -1.0;
        SeekTo(pos);
    } else {
        DownloadNextChunk();
    }
}

void NetworkStreamer::DownloadKey() {
    Logger::Log(LogLevel::INFO, "Downloading AES-128 key...");
    QNetworkRequest request(m_keyUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply && m_reply->error() == QNetworkReply::NoError) {
            m_aesKey = m_reply->readAll();
            Logger::Log(LogLevel::INFO, "AES key downloaded successfully.");

            m_reply->deleteLater();
            m_reply = nullptr;

            DownloadNextChunk();
        } else {
             Logger::Log(LogLevel::ERROR, "Failed to download AES key.");
        }
    });
}

void NetworkStreamer::DownloadNextChunk() {
    if (m_chunkQueue.isEmpty()) {
        Logger::Log(LogLevel::INFO, "All chunks downloaded for current track.");
        emit DownloadFinished();
        return;
    }

    m_currentChunkData.clear();

    QUrl nextUrl = m_chunkQueue.dequeue();
    QNetworkRequest request(nextUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);

    connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
}

void NetworkStreamer::OnReadyRead() {
    if (!m_reply || m_isPaused) return;

    if (m_streamType == StreamType::DirectHttp && m_totalFileSize == 0) {
        m_totalFileSize = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();

        if (m_pendingSeekPos > 0.0 && m_totalFileSize > 0 && m_trackDurationSec > 0) {
            double pos = m_pendingSeekPos;
            m_pendingSeekPos = -1.0;
            SeekTo(pos);
            return;
        }
    }

    QByteArray newData = m_reply->readAll();

    // Если трек зашифрован (AES) - копим чанк целиком
    if (m_isEncrypted) {
        m_currentChunkData.append(newData);
    } else {
        // Иначе стримим напрямую в аудио-движок
        if (!newData.isEmpty()) {
            emit DataReceived(newData);
        }
    }
}

void NetworkStreamer::OnChunkFinished() {
    bool shouldDownloadNextImmediately = false;

    if (m_reply && m_reply->error() == QNetworkReply::NoError) {
        QByteArray newData = m_reply->readAll();

        if (m_isEncrypted) {
            m_currentChunkData.append(newData);

            // Защита от мусорных HTML-страниц от ВК
            if (m_currentChunkData.startsWith("<!DOCTYPE") || m_currentChunkData.startsWith("<html")) {
                Logger::Log(LogLevel::ERROR, "NetworkStreamer: Returned HTML instead of audio!");
                shouldDownloadNextImmediately = true;
            } else {
                QByteArray chunkData = m_currentChunkData;
                QByteArray key = m_aesKey;
                QByteArray iv = m_aesIV;
                uint64_t seq = m_mediaSequence++;
                QPointer<NetworkStreamer> safeThis(this);

                m_currentChunkData.clear();

                // Отправляем чанк на расшифровку
                QThreadPool::globalInstance()->start([safeThis, chunkData, key, iv, seq]() mutable {
                    if (chunkData.isEmpty()) return;

                    uint8_t firstByte = static_cast<uint8_t>(chunkData[0]);
                    if (!(firstByte == 0x47 && chunkData.size() % 188 == 0)) {
                        int id3Size = 0;
                        if (chunkData.size() >= 10 && chunkData.startsWith("ID3")) {
                            const uint8_t* d = reinterpret_cast<const uint8_t*>(chunkData.constData());
                            id3Size = 10 + ((d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9]);
                            if (id3Size > chunkData.size()) id3Size = 0;
                        }

                        int cipherSize = chunkData.size() - id3Size;
                        if (cipherSize > 0) {
                            if (cipherSize % 16 != 0) {
                                int padding = 16 - (cipherSize % 16);
                                chunkData.append(QByteArray(padding, 0));
                                cipherSize += padding;
                            }

                            if (key.size() == 16) {
                                QByteArray currentIV = iv;
                                if (currentIV.isEmpty()) {
                                    currentIV = QByteArray(16, 0);
                                    uint64_t tempSeq = seq;
                                    for (int i = 15; i >= 8; --i) {
                                        currentIV[i] = tempSeq & 0xFF;
                                        tempSeq >>= 8;
                                    }
                                }

                                struct AES_ctx ctx;
                                AES_init_ctx_iv(&ctx, reinterpret_cast<const uint8_t*>(key.constData()),
                                                      reinterpret_cast<const uint8_t*>(currentIV.constData()));

                                uint8_t* cipherDataPtr = reinterpret_cast<uint8_t*>(chunkData.data()) + id3Size;
                                AES_CBC_decrypt_buffer(&ctx, cipherDataPtr, cipherSize);
                            } else {
                                chunkData.clear(); // Ключ битый, сбрасываем чанк
                            }
                        }
                    }

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, chunkData]() {
                        if (!safeThis) return;

                        if (!chunkData.isEmpty()) {
                            emit safeThis->DataReceived(chunkData);
                        }

                        safeThis->DownloadNextChunk();

                    }, Qt::QueuedConnection);
                });

                // Удаляем текущий сетевой ответ
                if (m_reply) {
                    m_reply->deleteLater();
                    m_reply = nullptr;
                }

                return;
            }
            m_currentChunkData.clear();
        } else {
            if (!newData.isEmpty()) {
                emit DataReceived(newData);
            }
            shouldDownloadNextImmediately = true;
        }
    } else {
        shouldDownloadNextImmediately = true;
    }

    // Очистка для синхронных веток
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (shouldDownloadNextImmediately) {
        DownloadNextChunk();
    }
}

void NetworkStreamer::DecryptAndPushChunk() {
    if (m_currentChunkData.isEmpty()) return;

    //Если файл уже является чистым MPEG-TS
    uint8_t firstByte = static_cast<uint8_t>(m_currentChunkData[0]);
    if (firstByte == 0x47 && m_currentChunkData.size() % 188 == 0) {
        Logger::Log(LogLevel::INFO, "VK sent raw MPEG-TS despite manifest! Bypassing AES.");
        emit DataReceived(m_currentChunkData);
        return;
    }

    int id3Size = 0;
    if (m_currentChunkData.size() >= 10 && m_currentChunkData.startsWith("ID3")) {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(m_currentChunkData.constData());
        id3Size = 10 + ((d[6] << 21) | (d[7] << 14) | (d[8] << 7) | d[9]);
        if (id3Size > m_currentChunkData.size()) id3Size = 0;
        Logger::Log(LogLevel::INFO, "Found unencrypted ID3 tag. Size: " + std::to_string(id3Size) + " bytes.");
    }

    int cipherSize = m_currentChunkData.size() - id3Size;
    if (cipherSize <= 0) {
        emit DataReceived(m_currentChunkData);
        return;
    }

    if (cipherSize % 16 != 0) {
        Logger::Log(LogLevel::WARNING, "Cipher size (" + std::to_string(cipherSize) + ") is not a multiple of 16. Padding...");
        int padding = 16 - (cipherSize % 16);
        m_currentChunkData.append(QByteArray(padding, 0));
        cipherSize += padding;
    }

    if (m_aesKey.size() != 16) {
        std::string err = "Invalid AES key size.";
        Logger::Log(LogLevel::ERROR, err);
        emit DownloadError(err);
        return;
    }

    QByteArray currentIV = m_aesIV;
    if (currentIV.isEmpty()) {
        currentIV = QByteArray(16, 0);
        uint64_t seq = m_mediaSequence;
        for (int i = 15; i >= 8; --i) {
            currentIV[i] = seq & 0xFF;
            seq >>= 8;
        }
    }
    m_mediaSequence++;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, reinterpret_cast<const uint8_t*>(m_aesKey.constData()),
                          reinterpret_cast<const uint8_t*>(currentIV.constData()));

    uint8_t* cipherDataPtr = reinterpret_cast<uint8_t*>(m_currentChunkData.data()) + id3Size;
    AES_CBC_decrypt_buffer(&ctx, cipherDataPtr, cipherSize);

    emit DataReceived(m_currentChunkData);
}

void NetworkStreamer::PauseDownload() {
    if (!m_isPaused) {
        m_isPaused = true;
        Logger::Log(LogLevel::INFO, "High Watermark reached. Pausing network read.");
    }
}

void NetworkStreamer::ResumeDownload() {
    if (m_isPaused) {
        m_isPaused = false;
        Logger::Log(LogLevel::INFO, "Low Watermark reached. Resuming network read.");
        if (m_reply && m_reply->bytesAvailable() > 0) {
            OnReadyRead();
        }
    }
}

void NetworkStreamer::OnErrorOccurred(QNetworkReply::NetworkError code) {
    if (m_reply) {
        std::string err = m_reply->errorString().toStdString();
        Logger::Log(LogLevel::ERROR, "Network error (" + std::to_string(code) + "): " + err);
        emit DownloadError(err);
    }
}

void NetworkStreamer::SeekTo(double targetSeconds) {
    if (m_hlsChunks.isEmpty() && m_totalFileSize == 0) {
        m_pendingSeekPos = targetSeconds;
        Logger::Log(LogLevel::INFO, "NetworkStreamer: Manifest not loaded yet. Seek deferred to " + std::to_string(targetSeconds) + "s.");
        return;
    }
    StopDownload();
    m_currentChunkData.clear();

    if (m_streamType == StreamType::DirectHttp) {
        if (m_totalFileSize > 0 && m_trackDurationSec > 0) {
            qint64 targetByte = static_cast<qint64>((targetSeconds / m_trackDurationSec) * m_totalFileSize);
            Logger::Log(LogLevel::INFO, "NetworkStreamer: [Direct HTTP] Requesting bytes=" + std::to_string(targetByte) + "-");

            QNetworkRequest request(m_baseUrl);
            request.setRawHeader("Range", QString("bytes=%1-").arg(targetByte).toUtf8());
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

            m_reply = m_manager->get(request);
            connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
            connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
            connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
        }
    }
    else if (m_streamType == StreamType::HlsEncrypted || m_streamType == StreamType::HlsUnencrypted) {
        double accumulatedTime = 0.0;
        int targetIndex = 0;

        for (int i = 0; i < m_hlsChunks.size(); ++i) {
            if (accumulatedTime + m_hlsChunks[i].durationSec > targetSeconds) {
                targetIndex = i;
                break;
            }

            if (i == m_hlsChunks.size() - 1) {
                targetIndex = i;
                break;
            }
            accumulatedTime += m_hlsChunks[i].durationSec;
        }

        if (targetSeconds > accumulatedTime + m_hlsChunks[targetIndex].durationSec) {
            targetSeconds = accumulatedTime + m_hlsChunks[targetIndex].durationSec - 0.1;
        }

        if (m_streamType == StreamType::HlsEncrypted) {
            m_mediaSequence = m_baseMediaSequence + targetIndex;
        }

        m_chunkQueue.clear();
        for (int i = targetIndex; i < m_hlsChunks.size(); ++i) {
            m_chunkQueue.enqueue(m_hlsChunks[i].url);
        }

        double skipSeconds = targetSeconds - accumulatedTime;
        if (skipSeconds < 0.0) skipSeconds = 0.0;

        emit ExactSeekOffset(skipSeconds);

        Logger::Log(LogLevel::INFO, "NetworkStreamer: Jumping to chunk " + std::to_string(targetIndex) + ". Skip PCM: " + std::to_string(skipSeconds) + "s");
        DownloadNextChunk();
    }
    else if (m_streamType == StreamType::EncryptedMonolith) {
        Logger::Log(LogLevel::WARNING, "NetworkStreamer: Monolith seek pending implementation.");
    }
}