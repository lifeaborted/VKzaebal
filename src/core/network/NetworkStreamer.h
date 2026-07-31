#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QQueue>       // ДОБАВЛЕНО
#include <QUrl>         // ДОБАВЛЕНО
#include <QStringList>  // ДОБАВЛЕНО
#include <string>

class NetworkStreamer : public QObject {
    Q_OBJECT
public:
    explicit NetworkStreamer(QObject* parent = nullptr);
    ~NetworkStreamer();

    void StartDownload(const std::string& url);
    void StopDownload();

    signals:
        void DataReceived(const QByteArray& data);
    void DownloadFinished();
    void DownloadError(const std::string& errorString);

public slots:
    void PauseDownload();
    void ResumeDownload();

private slots:
    void OnReadyRead();
    void OnFinished();
    void OnErrorOccurred(QNetworkReply::NetworkError code);

private:
    // НОВЫЕ МЕТОДЫ ДЛЯ HLS
    void ParseM3u8(const QString& manifestData, const QUrl& baseUrl);
    void DownloadNextChunk();

    bool m_isPaused = false;
    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply;

    QQueue<QUrl> m_chunkQueue; // ДОБАВЛЕНО: Очередь для хранения ссылок на куски аудио
};