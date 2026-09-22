#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QQueue>
#include <QUrl>
#include <QStringList>
#include <string>
#include <atomic>
#include <map>

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
    double durationSec = 0.0;
    bool isEncrypted = false;
    QUrl keyUrl;
    QByteArray iv;
    uint64_t mediaSequence = 0;
};

class NetworkStreamer : public QObject {
    Q_OBJECT
public:
    explicit NetworkStreamer(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    ~NetworkStreamer();

    void StartDownload(const std::string& url);
    void StopDownload();
    void SeekTo(double targetSeconds);
    
    void SetTrackDuration(int durationSec) { m_trackDurationSec = durationSec; }
    bool IsPaused() const { return m_isPaused; }

signals:
    void DataReceived(const QByteArray& data);
    void DownloadFinished();
    void DownloadError(const std::string& errorString);
    void ExactSeekOffset(double skipSeconds);

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
    void DownloadKey(const QUrl& keyUrl);
    void StartChunkDownload();
    void DeliverChunk(uint64_t seq, QByteArray data, uint64_t currentGen, bool wasEncrypted);

    bool m_isPaused = false;
    QNetworkAccessManager* m_manager;
    QNetworkReply* m_reply;

    // --- ПЕРЕМЕННЫЕ РОУТИНГА ---
    StreamType m_streamType = StreamType::Unknown;
    QUrl m_baseUrl;
    qint64 m_totalFileSize = 0;
    int m_trackDurationSec = 0;

    // --- ПЕРЕМЕННЫЕ ДЛЯ HLS ---
    QQueue<HlsChunk> m_chunkQueue;
    QVector<HlsChunk> m_hlsChunks;
    HlsChunk m_currentChunk;
    uint64_t m_baseMediaSequence = 0;
    QUrl m_loadedKeyUrl;
    QByteArray m_aesKey;
    QByteArray m_currentChunkData; // Буфер для накопления целого чанка
    double m_pendingSeekPos = -1.0;
    std::atomic<uint64_t> m_streamGeneration{0};

    // --- PIPELINING & REORDER BUFFER ---
    uint64_t m_nextEmitSequence = 0;
    std::map<uint64_t, QByteArray> m_readyChunks;
    int m_chunksInFlight = 0;
};