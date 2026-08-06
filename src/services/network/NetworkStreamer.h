#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QQueue>
#include <QUrl>
#include <QStringList>
#include <string>

// Подключаем C-библиотеку для дешифровки
extern "C" {
#include "aes.h"
}

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
    void OnChunkFinished();
    void OnErrorOccurred(QNetworkReply::NetworkError code);

private:
    void ParseM3u8(const QString& manifestData, const QUrl& baseUrl);
    void DownloadNextChunk();
    void DownloadKey();          // Скачивание AES-ключа
    void DecryptAndPushChunk();  // Расшифровка и передача данных

    bool m_isPaused = false;
    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply;

    QQueue<QUrl> m_chunkQueue;

    // --- ПЕРЕМЕННЫЕ ДЛЯ HLS AES-128 ---
    bool m_isEncrypted = false;
    uint64_t m_mediaSequence = 0;
    QUrl m_keyUrl;
    QByteArray m_aesKey;
    QByteArray m_aesIV;
    QByteArray m_currentChunkData; // Буфер для накопления целого чанка
};