#include "VkClient.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>

VkClient::VkClient(QObject* parent) : BaseApiProvider(parent) {
    Logger::Log(LogLevel::INFO, "VkClient created.");
}

VkClient::~VkClient() {
    Logger::Log(LogLevel::INFO, "VkClient destroyed.");
}

bool VkClient::HandleApiError(const QJsonDocument& json, int /*httpStatusCode*/) {
    QJsonObject root = json.object();
    if (root.contains("error")) {
        QJsonObject errObj = root["error"].toObject();
        int errCode = errObj["error_code"].toInt();
        std::string errMsg = errObj["error_msg"].toString().toStdString();

        Logger::Log(LogLevel::ERROR, "VK API Error [" + std::to_string(errCode) + "]: " + errMsg);

        if (errCode == 5) {
            emit TokenExpired();
        }
        return true;
    }
    return false;
}

void VkClient::ValidateToken(std::function<void(bool)> callback) {
    if (m_accessToken.empty()) { callback(false); return; }

    QUrl url("https://api.vk.com/method/users.get");
    QUrlQuery query;
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    SendJsonRequest(request, [callback](const QJsonDocument&) {
        Logger::Log(LogLevel::INFO, "api: Token is valid.");
        callback(true);
    }, [callback](const std::string&) {
        callback(false);
    });
}

void VkClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QUrl url("https://api.vk.com/method/audio.getById");
    QUrlQuery query;
    query.addQueryItem("audios", QString::fromStdString(trackId));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "VKAndroidApp/5.56.1-12345 (Android 11; SDK 30; x86_64; en; 2274003)");
    request.setTransferTimeout(5000);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        std::string freshUrl = "";
        QJsonArray responseArray = json.object()["response"].toArray();
        if (!responseArray.isEmpty()) {
            freshUrl = responseArray[0].toObject()["url"].toString().toStdString();
        }
        callback(freshUrl, false);
    }, [callback](const std::string&) {
        callback("", true);
    });
}

void VkClient::FetchAllUserAudio(int offset, int count) {
    QUrl url("https://api.vk.com/method/audio.get");
    QUrlQuery query;
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    query.addQueryItem("offset", QString::number(offset));
    query.addQueryItem("count", QString::number(count));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    SendJsonRequest(request, [this, offset, count](const QJsonDocument& json) {
        std::vector<Track> chunkTracks;
        QJsonArray items = json.object()["response"].toObject()["items"].toArray();
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            QJsonObject trackJson = val.toObject();
            Track track;

            int owner_id = trackJson["owner_id"].toInt();
            int audio_id = trackJson["id"].toInt();

            track.id = std::to_string(owner_id) + "_" + std::to_string(audio_id);
            track.source = "VK";
            track.ownerId = std::to_string(owner_id);
            track.artist = trackJson["artist"].toString().toStdString();
            track.title = trackJson["title"].toString().toStdString();
            track.url = trackJson["url"].toString().toStdString();
            track.duration = trackJson["duration"].toInt();
            track.coverUrl = "";
            track.lyrics_id = trackJson.contains("lyrics_id") ? std::to_string(trackJson["lyrics_id"].toInt()) : "";
            track.lyrics = "";

            if (trackJson.contains("album") && trackJson["album"].isObject()) {
                QJsonObject album = trackJson["album"].toObject();
                if (album.contains("thumb") && album["thumb"].isObject()) {
                    QJsonObject thumb = album["thumb"].toObject();
                    QStringList qualityKeys = {"photo_1200", "photo_600", "photo_300", "photo_270", "photo_135", "photo_68", "photo_34"};
                    for (const QString& key : qualityKeys) {
                        if (thumb.contains(key)) {
                            track.coverUrl = thumb[key].toString().toStdString();
                            break;
                        }
                    }
                }
            }

            if (audio_id != 0) chunkTracks.push_back(std::move(track));
        }

        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);

        if (items.size() == count) FetchAllUserAudio(offset + count, count);
        else emit FinishedFetching();

    }, [this](const std::string&) {
        emit FinishedFetching();
    });
}