#include "NetworkStreamer.h"
#include "utils/logger/logger.h"
#include <QNetworkRequest>
#include <QUrl>

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

    QUrl url(QString::fromStdString(urlString));
    QNetworkRequest request(url);

    // Запускаем GET-запрос
    m_reply = m_manager->get(request);

    // Подключаем события сети к нашим функциям
    connect(m_reply, &QNetworkReply::readyRead, this, &NetworkStreamer::OnReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &NetworkStreamer::OnFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &NetworkStreamer::OnErrorOccurred);
}

void NetworkStreamer::StopDownload() {
    if (m_reply) {
        Logger::Log(LogLevel::INFO, "Aborting network stream.");
        m_reply->abort();
        m_reply->deleteLater(); // Безопасное удаление объекта в Qt
        m_reply = nullptr;
    }
}

void NetworkStreamer::OnReadyRead() {
    if (!m_reply) return;

    // Считываем всё, что успело прилететь по TCP с момента прошлого вызова
    QByteArray data = m_reply->readAll();

    // Временно логируем каждый кусок, чтобы видеть, как льются данные
    Logger::Log(LogLevel::INFO, "Received network chunk: " + std::to_string(data.size()) + " bytes.");

    // Отправляем сырые байты наружу (пока в пустоту, позже мы направим их в кольцевой буфер)
    emit DataReceived(data);
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