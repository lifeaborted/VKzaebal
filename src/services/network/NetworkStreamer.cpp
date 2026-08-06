#include "NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>
#include <QRegularExpression>

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

    // РАЗРЕШАЕМ РЕДИРЕКТЫ
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);
    m_isPaused = false;

    // HSL playlist
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
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void NetworkStreamer::ParseM3u8(const QString& manifestData, const QUrl& baseUrl) {
    QStringList lines = manifestData.split('\n');

    QRegularExpression keyRegex("#EXT-X-KEY:METHOD=AES-128,URI=\"([^\"]+)\"(?:,IV=(?:0x)?([0-9a-fA-F]+))?");
    QRegularExpression seqRegex("#EXT-X-MEDIA-SEQUENCE:(\\d+)");

    // Ищем стартовый номер чанка
    QRegularExpressionMatch seqMatch = seqRegex.match(manifestData);
    if (seqMatch.hasMatch()) {
        m_mediaSequence = seqMatch.captured(1).toULongLong();
    }

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();

        QRegularExpressionMatch match = keyRegex.match(trimmed);
        if (match.hasMatch()) {
            m_isEncrypted = true;
            m_keyUrl = baseUrl.resolved(QUrl(match.captured(1)));

            if (!match.captured(2).isEmpty()) {
                m_aesIV = QByteArray::fromHex(match.captured(2).toUtf8());
            }
            continue;
        }

        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            continue;
        }
        // добавляем чанк в очередь
        QUrl chunkUrl = baseUrl.resolved(QUrl(trimmed));
        m_chunkQueue.enqueue(chunkUrl);
    }
    Logger::Log(LogLevel::INFO, "Parsed " + std::to_string(m_chunkQueue.size()) + " audio chunks. Encrypted: " + (m_isEncrypted ? "Yes" : "No"));
}

void NetworkStreamer::DownloadKey() {
    Logger::Log(LogLevel::INFO, "Downloading AES-128 key...");
    QNetworkRequest request(m_keyUrl);

    // редиректы для ключа
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

    // релиректы для чанка
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);

    connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnChunkFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
}

void NetworkStreamer::DecryptAndPushChunk() {
    if (m_aesKey.size() != 16) {
        Logger::Log(LogLevel::ERROR, "Invalid AES key size.");
        return;
    }

    int paddedSize = m_currentChunkData.size();
    if (paddedSize % 16 != 0) {
        Logger::Log(LogLevel::WARNING, "Chunk size is not a multiple of 16. Padding... (Check if redirect failed)");
        int padding = 16 - (paddedSize % 16);
        m_currentChunkData.append(QByteArray(padding, 0));
        paddedSize += padding;
    }

    // Определяем IV для текущего чанка
    QByteArray currentIV = m_aesIV;
    if (currentIV.isEmpty()) {
        currentIV = QByteArray(16, 0);
        uint64_t seq = m_mediaSequence;
        for (int i = 15; i >= 8; --i) {
            currentIV[i] = seq & 0xFF;
            seq >>= 8;
        }
    }

    // Увеличиваем номер для следующего чанка
    m_mediaSequence++;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, reinterpret_cast<const uint8_t*>(m_aesKey.constData()),
                          reinterpret_cast<const uint8_t*>(currentIV.constData()));

    AES_CBC_decrypt_buffer(&ctx, reinterpret_cast<uint8_t*>(m_currentChunkData.data()), paddedSize);

    emit DataReceived(m_currentChunkData);
}

void NetworkStreamer::OnReadyRead() {
    if (!m_reply || m_isPaused) return;

    m_currentChunkData.append(m_reply->readAll());
}

void NetworkStreamer::OnChunkFinished() {
    if (m_reply && m_reply->error() == QNetworkReply::NoError) {

        m_currentChunkData.append(m_reply->readAll());

        if (m_isEncrypted && m_currentChunkData.size() > 0) {
            DecryptAndPushChunk();
        } else {
            emit DataReceived(m_currentChunkData);
        }
    }

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    DownloadNextChunk();
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

        // Вручную дергаем чтение, чтобы выгрести то, что накопилось во время паузы
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