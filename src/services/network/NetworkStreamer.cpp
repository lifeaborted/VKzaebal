#include "NetworkStreamer.h"
#include "utils/logger/Logger.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>

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
        Logger::Log(LogLevel::WARNING, "Download already in progress. Stopping previous stream.");
        StopDownload();
    }

    m_chunkQueue.clear(); // Обязательно очищаем очередь от предыдущего трека

    QUrl url(QString::fromStdString(urlString));
    QNetworkRequest request(url);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);
    m_isPaused = false;

    // ЕСЛИ ЭТО HLS ПЛЕЙЛИСТ (.m3u8)
    if (urlString.find(".m3u8") != std::string::npos) {
        connect(m_reply, &QNetworkReply::finished, this, [this, url]() {
            if (m_reply && m_reply->error() == QNetworkReply::NoError) {
                QString manifest = m_reply->readAll();
                ParseM3u8(manifest, url);

                m_reply->deleteLater();
                m_reply = nullptr;

                DownloadNextChunk(); // Запускаем цепочку скачивания чанков
            } else {
                Logger::Log(LogLevel::ERROR, "Failed to download .m3u8 manifest");
            }
        });
        connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
    }
    // ЕСЛИ ЭТО ПРЯМАЯ ССЫЛКА (старая логика)
    else {
        connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
        connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnFinished);
        connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
    }
}

void NetworkStreamer::StopDownload() {
    m_chunkQueue.clear(); // Очищаем очередь принудительно при переключении трека
    if (m_reply) {
        Logger::Log(LogLevel::INFO, "Aborting network stream.");
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

// НОВЫЙ МЕТОД: Парсинг манифеста
void NetworkStreamer::ParseM3u8(const QString& manifestData, const QUrl& baseUrl) {
    QStringList lines = manifestData.split('\n');

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            continue;
        }
        QUrl chunkUrl = baseUrl.resolved(QUrl(trimmed));
        m_chunkQueue.enqueue(chunkUrl);
    }
    Logger::Log(LogLevel::INFO, "Parsed " + std::to_string(m_chunkQueue.size()) + " audio chunks.");
}

// НОВЫЙ МЕТОД: Скачивание кусков аудио
void NetworkStreamer::DownloadNextChunk() {
    if (m_chunkQueue.isEmpty()) {
        Logger::Log(LogLevel::INFO, "All chunks downloaded for current track.");
        emit DownloadFinished();
        return;
    }

    QUrl nextUrl = m_chunkQueue.dequeue();
    QNetworkRequest request(nextUrl);

    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(512 * 1024);

    // Переиспользуем твои родные слоты для чтения байтов в bass
    connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);

    // Когда кусок скачался — качаем следующий
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply) {
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        DownloadNextChunk();
    });
}

void NetworkStreamer::OnReadyRead() {
    // Если поток на паузе — просто игнорируем сигнал.
    if (!m_reply || m_isPaused) return;

    // Считываем всё, что успело прилететь по TCP с момента прошлого вызова
    QByteArray data = m_reply->readAll();

    // Временно логируем каждый кусок, чтобы видеть, как льются данные
    Logger::Log(LogLevel::INFO, "Received network chunk: " + std::to_string(data.size()) + " bytes.");

    // Отправляем сырые байты наружу (пока в пустоту, позже мы направим их в кольцевой буфер)
    emit DataReceived(data);
}

// Реализация паузы скачивания
void NetworkStreamer::PauseDownload() {
    if (!m_isPaused) {
        m_isPaused = true;
        Logger::Log(LogLevel::INFO, "High Watermark reached. Pausing network read.");
    }
}

// Реализация восстановления скачивания
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

void NetworkStreamer::OnFinished() {
    Logger::Log(LogLevel::INFO, "Network stream finished.");
    emit DownloadFinished();

    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void NetworkStreamer::OnErrorOccurred(QNetworkReply::NetworkError code) {
    if (m_reply) {
        std::string err = m_reply->errorString().toStdString();
        Logger::Log(LogLevel::ERROR, "Network error (" + std::to_string(code) + "): " + err);
        emit DownloadError(err);
    }
}