#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <string>

class NetworkStreamer : public QObject {
    Q_OBJECT
public:
    explicit NetworkStreamer(QObject* parent = nullptr);
    ~NetworkStreamer();

    void StartDownload(const std::string& url);
    void StopDownload();

    // Сигналы, которые класс отправляет наружу, когда что-то происходит
    signals:
    void DataReceived(const QByteArray& data);
    void DownloadFinished();
    void DownloadError(const std::string& errorString);


    // Слоты (обработчики событий внутри сети)
public slots:
    void PauseDownload();
    void ResumeDownload();

private slots:
    void OnReadyRead();
    void OnFinished();
    void OnErrorOccurred(QNetworkReply::NetworkError code);


private:
    bool m_isPaused = false;
    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply;
};