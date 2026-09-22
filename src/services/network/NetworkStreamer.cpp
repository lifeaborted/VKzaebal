#include "NetworkStreamer.h"
#include "utils/parser/Id3Utils.h"
#include "utils/logger/Logger.h"

#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>
#include <QStringTokenizer>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QThreadPool>
#include <QPointer>
#include <QCoreApplication>

NetworkStreamer::NetworkStreamer(QObject* parent, QNetworkAccessManager* manager)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)), m_reply(nullptr) {
    Logger::Log(LogLevel::INFO, "NetworkStreamer created.");
}

NetworkStreamer::~NetworkStreamer() {
    StopDownload();
    Logger::Log(LogLevel::INFO, "NetworkStreamer destroyed.");
}

void NetworkStreamer::StartDownload(const std::string& urlString) {
    Logger::Log(LogLevel::INFO, "Starting network stream from: " + urlString);
    m_streamGeneration.fetch_add(1, std::memory_order_relaxed);
    m_pendingSeekPos = -1.0;
    m_totalFileSize = 0;

    if (m_reply) {
        StopDownload();
    }
    m_streamType = StreamType::DirectHttp;
    m_baseUrl = QUrl(QString::fromStdString(urlString));

    m_chunkQueue.clear();
    m_hlsChunks.clear();
    m_currentChunk = HlsChunk();
    m_loadedKeyUrl = QUrl();
    m_aesKey.clear();
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
                m_reply->deleteLater();
                m_reply = nullptr;

                ParseM3u8(manifest, url);
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
    m_streamGeneration.fetch_add(1, std::memory_order_relaxed);
    m_chunkQueue.clear();
    m_currentChunk = HlsChunk();
    m_isPaused = false;
    m_readyChunks.clear();
    m_chunksInFlight = 0;
    if (m_reply) {
        Logger::Log(LogLevel::INFO, "Aborting network stream.");
        
        m_reply->disconnect();

        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void NetworkStreamer::ParseM3u8(const QString& manifestData, const QUrl& baseUrl) {
    QRegularExpression keyRegex("#EXT-X-KEY:METHOD=([A-Za-z0-9_-]+)(?:,URI=\"([^\"]+)\")?(?:,IV=(?:0x)?([0-9a-fA-F]+))?");
    QRegularExpression seqRegex("#EXT-X-MEDIA-SEQUENCE:(\\d+)");

    uint64_t currentMediaSeq = 0;
    QRegularExpressionMatch seqMatch = seqRegex.match(manifestData);
    if (seqMatch.hasMatch()) {
        currentMediaSeq = seqMatch.captured(1).toULongLong();
    }
    m_baseMediaSequence = currentMediaSeq;
    m_nextEmitSequence = currentMediaSeq;
    m_readyChunks.clear();
    m_chunksInFlight = 0;

    QUrlQuery baseQuery(baseUrl);
    m_hlsChunks.clear();
    m_chunkQueue.clear();

    bool currentIsEncrypted = false;
    QUrl currentKeyUrl;
    QByteArray currentIv;
    double currentDuration = 0.0;
    bool hasAnyEncrypted = false;

    for (auto rawLine : QStringTokenizer{manifestData, u'\n'}) {
        QStringView trimmed = rawLine.trimmed();

        QRegularExpressionMatch match = keyRegex.match(trimmed);
        if (match.hasMatch()) {
            QString method = match.captured(1).toUpper();
            if (method == "NONE") {
                currentIsEncrypted = false;
                currentKeyUrl = QUrl();
                currentIv.clear();
            } else if (method == "AES-128") {
                currentIsEncrypted = true;
                hasAnyEncrypted = true;
                currentKeyUrl = baseUrl.resolved(QUrl(match.captured(2)));

                QUrlQuery keyQuery(currentKeyUrl);
                for (const auto& item : baseQuery.queryItems()) {
                    if (!keyQuery.hasQueryItem(item.first)) {
                        keyQuery.addQueryItem(item.first, item.second);
                    }
                }
                currentKeyUrl.setQuery(keyQuery);

                if (!match.captured(3).isEmpty()) {
                    currentIv = QByteArray::fromHex(match.captured(3).toUtf8());
                } else {
                    currentIv.clear();
                }
            }
            continue;
        }

        if (trimmed.startsWith(u"#EXTINF:")) {
            QStringView valStr = trimmed.sliced(8);
            auto commaPos = valStr.indexOf(u',');
            if (commaPos != -1) valStr = valStr.first(commaPos);
            currentDuration = valStr.toDouble();
            continue;
        }

        if (trimmed.isEmpty() || trimmed.startsWith(u'#')) continue;

        QUrl chunkUrl = baseUrl.resolved(QUrl(trimmed.toString()));
        QUrlQuery chunkQuery(chunkUrl);
        for (const auto& item : baseQuery.queryItems()) {
            if (!chunkQuery.hasQueryItem(item.first)) {
                chunkQuery.addQueryItem(item.first, item.second);
            }
        }
        chunkUrl.setQuery(chunkQuery);

        HlsChunk chunk;
        chunk.url = chunkUrl;
        chunk.durationSec = currentDuration;
        chunk.isEncrypted = currentIsEncrypted;
        chunk.keyUrl = currentKeyUrl;
        chunk.iv = currentIv;
        chunk.mediaSequence = currentMediaSeq++;

        m_hlsChunks.push_back(chunk);
        m_chunkQueue.enqueue(chunk);
    }

    if (m_hlsChunks.size() == 1 && hasAnyEncrypted) {
        m_streamType = StreamType::EncryptedMonolith;
    } else if (hasAnyEncrypted) {
        m_streamType = StreamType::HlsEncrypted;
    } else {
        m_streamType = StreamType::HlsUnencrypted;
    }

    Logger::Log(LogLevel::INFO, "Parsed " + std::to_string(m_hlsChunks.size()) + " audio chunks. Has Encrypted: " + (hasAnyEncrypted ? "Yes" : "No"));
    if (m_pendingSeekPos >= 0.0) {
        double pos = m_pendingSeekPos;
        m_pendingSeekPos = -1.0;
        SeekTo(pos);
    } else {
        DownloadNextChunk();
    }
}

void NetworkStreamer::DownloadKey(const QUrl& keyUrl) {
    Logger::Log(LogLevel::INFO, "Downloading AES-128 key: " + keyUrl.toString().toStdString());
    m_loadedKeyUrl = keyUrl;
    QNetworkRequest request(keyUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply && m_reply->error() == QNetworkReply::NoError) {
            m_aesKey = m_reply->readAll();
            Logger::Log(LogLevel::INFO, "AES key downloaded successfully.");

            m_reply->deleteLater();
            m_reply = nullptr;

            StartChunkDownload();
        } else {
            Logger::Log(LogLevel::ERROR, "Failed to download AES key.");
        }
    });
}

void NetworkStreamer::DownloadNextChunk() {
    if (m_isPaused) {
        Logger::Log(LogLevel::INFO, "NetworkStreamer: Download paused (watermark). Not dequeuing next chunk.");
        return;
    }

    if (m_chunkQueue.isEmpty()) {
        if (m_readyChunks.empty() && !m_reply && m_chunksInFlight == 0) {
            Logger::Log(LogLevel::INFO, "All chunks downloaded and processed for current track.");
            emit DownloadFinished();
        }
        return;
    }

    m_currentChunk = m_chunkQueue.dequeue();

    if (m_currentChunk.isEncrypted && (m_aesKey.isEmpty() || m_loadedKeyUrl != m_currentChunk.keyUrl)) {
        DownloadKey(m_currentChunk.keyUrl);
        return;
    }

    StartChunkDownload();
}

void NetworkStreamer::StartChunkDownload() {
    m_currentChunkData.clear();
    m_currentChunkData.reserve(128 * 1024);

    Logger::Log(LogLevel::INFO, "StartChunkDownload: seq=" + std::to_string(m_currentChunk.mediaSequence) +
                " url=" + m_currentChunk.url.fileName().toStdString());

    QNetworkRequest request(m_currentChunk.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);

    connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
}

void NetworkStreamer::OnReadyRead() {
    if (!m_reply) return;

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

    if (m_streamType != StreamType::DirectHttp) {
        m_currentChunkData.append(newData);
    } else {
        if (!newData.isEmpty()) {
            emit DataReceived(newData);
        }
    }
}

void NetworkStreamer::OnChunkFinished() {
    if (!m_reply) return;

    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    Logger::Log(LogLevel::INFO, "OnChunkFinished: url=" + reply->url().fileName().toStdString() +
                " err=" + std::to_string(reply->error()) +
                " bytes=" + std::to_string(reply->bytesAvailable()));

    if (reply->error() != QNetworkReply::NoError) {
        Logger::Log(LogLevel::ERROR, "OnChunkFinished network error: " + reply->errorString().toStdString());
        if (m_streamType == StreamType::DirectHttp) {
            emit DownloadFinished();
        } else {
            if (!m_isPaused && !m_chunkQueue.isEmpty()) {
                DownloadNextChunk();
            } else if (m_chunkQueue.isEmpty() && m_readyChunks.empty() && m_chunksInFlight == 0) {
                emit DownloadFinished();
            }
        }
        return;
    }

    QByteArray newData = reply->readAll();
    if (m_streamType == StreamType::DirectHttp) {
        if (!newData.isEmpty()) {
            emit DataReceived(newData);
        }
        emit DownloadFinished();
        return;
    }

    m_currentChunkData.append(newData);

    // HLS Stream
    HlsChunk completedChunk = m_currentChunk;
    QByteArray chunkData = std::move(m_currentChunkData);
    m_currentChunkData.clear();

    uint64_t seq = completedChunk.mediaSequence;
    uint64_t currentGen = m_streamGeneration.load(std::memory_order_relaxed);

    // --- PIPELINING: Скачивание следующего чанка запускаем СРАЗУ, не дожидаясь окончания дешифровки! ---
    if (!m_isPaused && !m_chunkQueue.isEmpty()) {
        DownloadNextChunk();
    }

    if (completedChunk.isEncrypted) {
        // Защита от мусорных HTML-страниц от ВК
        if (chunkData.startsWith("<!DOCTYPE") || chunkData.startsWith("<html")) {
            Logger::Log(LogLevel::ERROR, "NetworkStreamer: Returned HTML instead of audio!");
            DeliverChunk(seq, QByteArray(), currentGen, false);
            return;
        }

        m_chunksInFlight++;
        QByteArray key = m_aesKey;
        QByteArray iv = completedChunk.iv;
        QPointer<NetworkStreamer> safeThis(this);

        QThreadPool::globalInstance()->start([safeThis, chunkData = std::move(chunkData), key, iv, seq, currentGen]() mutable {
            if (chunkData.isEmpty() || !safeThis || safeThis->m_streamGeneration.load(std::memory_order_relaxed) != currentGen) {
                if (safeThis) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, seq, currentGen]() {
                        if (safeThis) safeThis->DeliverChunk(seq, QByteArray(), currentGen, true);
                    }, Qt::QueuedConnection);
                }
                return;
            }

            uint8_t firstByte = static_cast<uint8_t>(chunkData[0]);
            bool willDecrypt = !(firstByte == 0x47 && chunkData.size() % 188 == 0);
            if (willDecrypt) {
                int id3Size = 0;
                size_t parsedId3 = Id3Utils::ParseHeaderTotalSize(
                    reinterpret_cast<const uint8_t*>(chunkData.constData()),
                    static_cast<size_t>(chunkData.size()));
                if (parsedId3 > 0 && parsedId3 <= static_cast<size_t>(chunkData.size())) {
                    id3Size = static_cast<int>(parsedId3);
                }

                int cipherSize = chunkData.size() - id3Size;
                if (cipherSize > 0) {
                    if (cipherSize % 16 != 0) {
                        int padding = 16 - (cipherSize % 16);
                        chunkData.append(padding, '\0');
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

                        // Удаляем PKCS#7 паддинг после дешифровки AES-128
                        if (chunkData.size() > id3Size) {
                            uint8_t pad = static_cast<uint8_t>(chunkData[chunkData.size() - 1]);
                            if (pad > 0 && pad <= 16 && (chunkData.size() - id3Size) >= pad) {
                                bool validPad = true;
                                for (int p = 0; p < pad; ++p) {
                                    if (static_cast<uint8_t>(chunkData[chunkData.size() - 1 - p]) != pad) {
                                        validPad = false;
                                        break;
                                    }
                                }
                                if (validPad) {
                                    chunkData.chop(pad);
                                }
                            }
                        }

                        // Удаляем ID3-тег, если он предшествовал аудиоданным
                        if (id3Size > 0 && chunkData.size() >= id3Size) {
                            chunkData.remove(0, id3Size);
                        }

                        // Для MPEG-TS потоков: отсекаем неполные TS-пакеты на конце чанка
                        if (chunkData.size() >= 188 && static_cast<uint8_t>(chunkData[0]) == 0x47) {
                            size_t remainder = chunkData.size() % 188;
                            if (remainder != 0) {
                                chunkData.chop(static_cast<qsizetype>(remainder));
                            }
                        }
                    } else {
                        chunkData.clear();
                    }
                }
            }

            if (!safeThis || safeThis->m_streamGeneration.load(std::memory_order_relaxed) != currentGen) {
                if (safeThis) {
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, seq, currentGen]() {
                        if (safeThis) safeThis->DeliverChunk(seq, QByteArray(), currentGen, true);
                    }, Qt::QueuedConnection);
                }
                return;
            }

            QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, chunkData = std::move(chunkData), seq, currentGen]() mutable {
                if (safeThis) {
                    safeThis->DeliverChunk(seq, std::move(chunkData), currentGen, true);
                }
            }, Qt::QueuedConnection);
        });
    } else {
        // Незашифрованный HLS чанк (например, seg-01 или seg-02 в выборочно зашифрованном манифесте ВК)
        if (!chunkData.isEmpty()) {
            size_t parsedId3 = Id3Utils::ParseHeaderTotalSize(
                reinterpret_cast<const uint8_t*>(chunkData.constData()),
                static_cast<size_t>(chunkData.size()));
            if (parsedId3 > 0 && parsedId3 <= static_cast<size_t>(chunkData.size())) {
                chunkData.remove(0, static_cast<qsizetype>(parsedId3));
            }
            if (chunkData.size() >= 188 && static_cast<uint8_t>(chunkData[0]) == 0x47) {
                size_t rem = chunkData.size() % 188;
                if (rem != 0) {
                    chunkData.chop(static_cast<qsizetype>(rem));
                }
            }
        }
        DeliverChunk(seq, std::move(chunkData), currentGen, false);
    }
}

void NetworkStreamer::DeliverChunk(uint64_t seq, QByteArray data, uint64_t currentGen, bool wasEncrypted) {
    if (m_streamGeneration.load(std::memory_order_relaxed) != currentGen) {
        return;
    }

    if (wasEncrypted) {
        m_chunksInFlight--;
        if (m_chunksInFlight < 0) m_chunksInFlight = 0;
    }

    m_readyChunks[seq] = std::move(data);

    // Дрейним реордер-буфер строго по возрастанию seq
    while (!m_readyChunks.empty() && m_readyChunks.begin()->first == m_nextEmitSequence) {
        auto it = m_readyChunks.begin();
        QByteArray chunkToEmit = std::move(it->second);
        uint64_t emittedSeq = it->first;
        m_readyChunks.erase(it);
        m_nextEmitSequence++;

        if (!chunkToEmit.isEmpty()) {
            Logger::Log(LogLevel::INFO, "Emitted chunk seq=" + std::to_string(emittedSeq) + " size=" + std::to_string(chunkToEmit.size()));
            emit DataReceived(chunkToEmit);
        }
    }

    if (m_chunkQueue.isEmpty() && m_readyChunks.empty() && !m_reply && m_chunksInFlight == 0) {
        Logger::Log(LogLevel::INFO, "All chunks downloaded and processed for current track.");
        emit DownloadFinished();
    }
}

void NetworkStreamer::PauseDownload() {
    if (m_streamType == StreamType::DirectHttp) {
        return;
    }
    if (!m_isPaused) {
        m_isPaused = true;
        Logger::Log(LogLevel::INFO, "High Watermark reached. Pausing network read.");
    }
}

void NetworkStreamer::ResumeDownload() {
    if (m_isPaused) {
        m_isPaused = false;
        Logger::Log(LogLevel::INFO, "Low Watermark reached. Resuming network read.");
        if (m_streamType == StreamType::DirectHttp) {
            if (m_reply && m_reply->bytesAvailable() > 0) {
                OnReadyRead();
            }
        } else {
            if (!m_reply && !m_chunkQueue.isEmpty()) {
                DownloadNextChunk();
            }
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



        m_chunkQueue.clear();
        for (int i = targetIndex; i < m_hlsChunks.size(); ++i) {
            m_chunkQueue.enqueue(m_hlsChunks[i]);
        }
        m_nextEmitSequence = m_hlsChunks[targetIndex].mediaSequence;
        m_readyChunks.clear();
        m_chunksInFlight = 0;

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