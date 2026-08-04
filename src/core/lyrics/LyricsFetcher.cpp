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

void LyricsFetcher::FetchLyrics(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback) {
    if (artist.empty() || title.empty()) {
        callback("");
        return;
    }

    QUrl url("https://lrclib.net/api/search");
    QUrlQuery query;
    query.addQueryItem("artist_name", QString::fromStdString(artist));
    query.addQueryItem("track_name", QString::fromStdString(title));
    url.setQuery(query);

    QNetworkRequest request(url);
    // LRCLIB требует адекватный User-Agent
    request.setRawHeader("User-Agent", "VKAudioPlayer/1.0 (C++ Qt)");

    QNetworkReply* reply = m_manager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        std::string lyricsText = "";

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray raw = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(raw);
            
            if (doc.isArray()) {
                QJsonArray arr = doc.array();
                if (!arr.isEmpty()) {
                    QJsonObject firstHit = arr[0].toObject();
                    QString plainLyrics = firstHit["plainLyrics"].toString();
                    if (!plainLyrics.isEmpty()) {
                        lyricsText = plainLyrics.toStdString();
                        Logger::Log(LogLevel::INFO, "LyricsFetcher: Successfully found lyrics.");
                    } else {
                        Logger::Log(LogLevel::WARNING, "LyricsFetcher: Track found, but no plain text available.");
                    }
                } else {
                    Logger::Log(LogLevel::WARNING, "LyricsFetcher: No lyrics found for this track.");
                }
            }
        } else {
            Logger::Log(LogLevel::ERROR, "LyricsFetcher Network Error: " + reply->errorString().toStdString());
        }

        callback(lyricsText);
        reply->deleteLater();
    });
}