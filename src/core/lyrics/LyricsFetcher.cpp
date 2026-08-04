#include "LyricsFetcher.h"
#include "utils/logger/logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

LyricsFetcher::LyricsFetcher(QObject* parent) 
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "LyricsFetcher created.");
}

LyricsFetcher::~LyricsFetcher() {
    Logger::Log(LogLevel::INFO, "LyricsFetcher destroyed.");
}

// ЭТАП 1: Точный поиск (LRCLIB /api/get)
void LyricsFetcher::FetchLyrics(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback) {
    if (artist.empty() || title.empty()) {
        callback("");
        return;
    }

    QUrl url("https://lrclib.net/api/get");
    QUrlQuery query;
    query.addQueryItem("artist_name", QString::fromStdString(artist));
    query.addQueryItem("track_name", QString::fromStdString(title));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VKAudioPlayer/1.0 (C++ Qt)");

    QNetworkReply* reply = m_manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, artist, title, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject root = doc.object();

            QString synced = root["syncedLyrics"].toString();
            QString plain = root["plainLyrics"].toString();
            QString result = !synced.isEmpty() ? synced : plain;

            if (!result.isEmpty()) {
                Logger::Log(LogLevel::INFO, "LyricsFetcher: Exact match found in LRCLIB.");
                callback(result.toStdString());
                reply->deleteLater();
                return;
            }
        }

        reply->deleteLater();
        SearchLrcLibFallback(artist, title, callback);
    });
}

// ЭТАП 2: Нечеткий поиск (LRCLIB /api/search)
void LyricsFetcher::SearchLrcLibFallback(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback) {
    QUrl url("https://lrclib.net/api/search");
    QUrlQuery query;
    query.addQueryItem("q", QString::fromStdString(artist + " " + title));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VKAudioPlayer/1.0 (C++ Qt)");

    QNetworkReply* reply = m_manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isArray() && !doc.array().isEmpty()) {
                QJsonObject firstHit = doc.array()[0].toObject();
                QString synced = firstHit["syncedLyrics"].toString();
                QString plain = firstHit["plainLyrics"].toString();
                QString result = !synced.isEmpty() ? synced : plain;

                if (!result.isEmpty()) {
                    Logger::Log(LogLevel::INFO, "LyricsFetcher: Search match found in LRCLIB.");
                    callback(result.toStdString());
                    reply->deleteLater();
                    return;
                }
            }
        }

        // Если и тут пусто — отдаем пустую строку и не мучаем систему
        Logger::Log(LogLevel::WARNING, "LyricsFetcher: No lyrics found in LRCLIB databases.");
        callback("");
        reply->deleteLater();
    });
}