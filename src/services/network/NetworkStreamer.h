#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QQueue>
#include <QUrl>
#include <QStringList>
#include <string>

extern "C" {
#include "aes.h"
}

enum class StreamType {
    Unknown,
    DirectHttp,         // Открытые MP3
    HlsUnencrypted,     // Обычный m3u8
    HlsEncrypted,       // Зашифрованный m3u8
    EncryptedMonolith   // Зашифрованный m3u8, 1 чанк
};

struct HlsChunk {
    QUrl url;
    double durationSec;
};

class NetworkStreamer : public QObject {
    Q_OBJECT
public:
    explicit NetworkStreamer(QObject* parent = nullptr);
    ~NetworkStreamer();

    void StartDownload(const std::string& url);
    void StopDownload();
    void SeekTo(double targetSeconds);

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

    // --- ПЕРЕМЕННЫЕ РОУТИНГА ---
    StreamType m_streamType = StreamType::Unknown;
    QUrl m_baseUrl;
    qint64 m_totalFileSize = 0;
    int m_trackDurationSec = 0;

    // --- ПЕРЕМЕННЫЕ ДЛЯ HLS AES-128 ---
    QQueue<QUrl> m_chunkQueue;
    QVector<HlsChunk> m_hlsChunks;
    uint64_t m_baseMediaSequence = 0;
    bool m_isEncrypted = false;
    uint64_t m_mediaSequence = 0;
    QUrl m_keyUrl;
    QByteArray m_aesKey;
    QByteArray m_aesIV;
    QByteArray m_currentChunkData; // Буфер для накопления целого чанка
};