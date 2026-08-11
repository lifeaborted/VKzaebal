#include "NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>
#include <QRegularExpression>
#include <QUrlQuery>

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

    if (m_reply) {
        StopDownload();
    }

    m_chunkQueue.clear();
    m_isEncrypted = false;
    m_mediaSequence = 0;
    m_aesKey.clear();
    m_aesIV.clear();
    m_currentChunkData.clear();

    QUrl url(QString::fromStdString(urlString));
    QNetworkRequest request(url);

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

    // Ищем номер чанка
    QRegularExpressionMatch seqMatch = seqRegex.match(manifestData);
    if (seqMatch.hasMatch()) {
        m_mediaSequence = seqMatch.captured(1).toULongLong();
    }

    // Достаем параметры из исходной ссылки
    QUrlQuery baseQuery(baseUrl);

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();

        QRegularExpressionMatch match = keyRegex.match(trimmed);
        if (match.hasMatch()) {
            m_isEncrypted = true;
            QUrl keyUrlResolved = baseUrl.resolved(QUrl(match.captured(1)));

            // слияние параметров запроса для ключа
            QUrlQuery keyQuery(keyUrlResolved);
            for (const auto& item : baseQuery.queryItems()) {
                if (!keyQuery.hasQueryItem(item.first)) {
                    keyQuery.addQueryItem(item.first, item.second);
                }
            }
            keyUrlResolved.setQuery(keyQuery);
            m_keyUrl = keyUrlResolved;

            if (!match.captured(2).isEmpty()) {
                m_aesIV = QByteArray::fromHex(match.captured(2).toUtf8());
            }
            continue;
        }

        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            continue;
        }

        QUrl chunkUrl = baseUrl.resolved(QUrl(trimmed));

        // слияние параметров запроса для чанка
        QUrlQuery chunkQuery(chunkUrl);
        for (const auto& item : baseQuery.queryItems()) {
            if (!chunkQuery.hasQueryItem(item.first)) {
                chunkQuery.addQueryItem(item.first, item.second);
            }
        }
        chunkUrl.setQuery(chunkQuery);

        m_chunkQueue.enqueue(chunkUrl);
    }
    Logger::Log(LogLevel::INFO, "Parsed " + std::to_string(m_chunkQueue.size()) + " audio chunks. Encrypted: " + (m_isEncrypted ? "Yes" : "No"));
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
    m_currentChunkData.append(m_reply->readAll());
}

void NetworkStreamer::OnChunkFinished() {
    if (m_reply && m_reply->error() == QNetworkReply::NoError) {
        m_currentChunkData.append(m_reply->readAll());

        if (m_currentChunkData.size() > 0) {
            if (m_currentChunkData.startsWith("<!DOCTYPE") || m_currentChunkData.startsWith("<html")) {
                Logger::Log(LogLevel::ERROR, "VK returned an HTML error page instead of an audio chunk!");
            }
            else if (m_isEncrypted) {
                DecryptAndPushChunk();
            } else {
                emit DataReceived(m_currentChunkData);
            }
        }
    }

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    DownloadNextChunk();
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
        Logger::Log(LogLevel::ERROR, "Invalid AES key size.");
        emit DataReceived(m_currentChunkData);
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