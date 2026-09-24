#include "VkClient.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>

VkClient::VkClient(QObject* parent, QNetworkAccessManager* manager) : BaseApiProvider(parent, manager) {
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

        if (errMsg.find("user is blocked") != std::string::npos || errMsg.find("User is deactivated") != std::string::npos) {
            Logger::Log(LogLevel::ERROR, "VK: Account is frozen/blocked by VK anti-fraud: " + errMsg);
            emit UserBlocked("VK", errMsg);
            return true;
        }

        if ((errCode == 5 || errCode == 15 || errCode == 27) && !m_isValidatingToken) {
            emit TokenExpired();
        }
        return true;
    }
    return false;
}

namespace {
constexpr const char* kVkApiUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
}

void VkClient::ValidateToken(std::function<void(bool)> callback) {
    if (m_accessToken.empty()) { callback(false); return; }

    m_isValidatingToken = true;

    QUrl url("https://api.vk.com/method/users.get");
    QUrlQuery query;
    query.addQueryItem("v", QString::fromStdString(m_apiVersion));
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    query.addQueryItem("fields", "deactivated");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kVkApiUserAgent);

    SendJsonRequest(request, [this, callback](const QJsonDocument& json) {
        m_isValidatingToken = false;
        QJsonArray users = json.object()["response"].toArray();
        if (!users.isEmpty()) {
            QJsonObject u = users[0].toObject();
            if (u.contains("deactivated")) {
                std::string status = u["deactivated"].toString().toStdString();
                Logger::Log(LogLevel::ERROR, "VK: Account is deactivated/frozen: " + status);
                emit UserBlocked("VK", "User account is " + status);
                callback(false);
                return;
            }
        }
        Logger::Log(LogLevel::INFO, "api: Token is valid.");
        callback(true);
    }, [this, callback](const std::string&) {
        m_isValidatingToken = false;
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
    request.setRawHeader("User-Agent", kVkApiUserAgent);
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

std::vector<Track> VkClient::ParseVkTracks(const QJsonArray& items) {
    std::vector<Track> tracks;
    tracks.reserve(items.size());

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

        if (audio_id != 0) tracks.push_back(std::move(track));
    }
    return tracks;
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
    request.setRawHeader("User-Agent", kVkApiUserAgent);

    SendJsonRequest(request, [this, offset, count](const QJsonDocument& json) {
        QJsonArray items = json.object()["response"].toObject()["items"].toArray();
        std::vector<Track> chunkTracks = ParseVkTracks(items);

        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);

        if (items.size() == count) FetchAllUserAudio(offset + count, count);
        else emit FinishedFetching();

    }, [this](const std::string&) {
        emit FinishedFetching();
    });
}

void VkClient::SearchAudio(const std::string& query, int count, int offset,
                           std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback({}, "VK access token is empty");
        return;
    }

    QUrl url("https://api.vk.com/method/audio.search");
    QUrlQuery q;
    q.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    q.addQueryItem("v", QString::fromStdString(m_apiVersion));
    q.addQueryItem("q", QString::fromStdString(query));
    q.addQueryItem("count", QString::number(count));
    q.addQueryItem("offset", QString::number(offset));
    q.addQueryItem("auto_complete", "1");
    q.addQueryItem("sort", "2");
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kVkApiUserAgent);
    request.setTransferTimeout(8000);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        QJsonArray items = json.object()["response"].toObject()["items"].toArray();
        std::vector<Track> tracks = ParseVkTracks(items);
        if (callback) callback(tracks, "");
    }, [callback](const std::string& err) {
        if (callback) callback({}, err);
    });
}

void VkClient::AddTrackToFavorites(const std::string& trackId, const std::string& ownerId,
                                   std::function<void(bool success, const std::string& error)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false, "VK access token is empty");
        return;
    }

    int audioIdNum = 0;
    int ownerIdNum = 0;

    auto underscorePos = trackId.find('_');
    if (underscorePos != std::string::npos) {
        try {
            ownerIdNum = std::stoi(trackId.substr(0, underscorePos));
            audioIdNum = std::stoi(trackId.substr(underscorePos + 1));
        } catch (...) {}
    } else {
        try {
            audioIdNum = std::stoi(trackId);
            if (!ownerId.empty()) ownerIdNum = std::stoi(ownerId);
        } catch (...) {}
    }

    if (audioIdNum == 0) {
        if (callback) callback(false, "Invalid VK track ID: " + trackId);
        return;
    }

    QUrl url("https://api.vk.com/method/audio.add");
    QUrlQuery q;
    q.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    q.addQueryItem("v", QString::fromStdString(m_apiVersion));
    q.addQueryItem("audio_id", QString::number(audioIdNum));
    q.addQueryItem("owner_id", QString::number(ownerIdNum));
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kVkApiUserAgent);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        if (json.object().contains("response")) {
            if (callback) callback(true, "");
        } else {
            std::string err = json.object().contains("error")
                ? json.object()["error"].toObject()["error_msg"].toString().toStdString()
                : "Unknown error adding track";
            if (callback) callback(false, err);
        }
    }, [callback](const std::string& err) {
        if (callback) callback(false, err);
    });
}

void VkClient::RemoveTrackFromFavorites(const std::string& trackId, const std::string& ownerId,
                                      std::function<void(bool success, const std::string& error)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false, "VK access token is empty");
        return;
    }

    int audioIdNum = 0;
    int ownerIdNum = 0;

    auto underscorePos = trackId.find('_');
    if (underscorePos != std::string::npos) {
        try {
            ownerIdNum = std::stoi(trackId.substr(0, underscorePos));
            audioIdNum = std::stoi(trackId.substr(underscorePos + 1));
        } catch (...) {}
    } else {
        try {
            audioIdNum = std::stoi(trackId);
            if (!ownerId.empty()) ownerIdNum = std::stoi(ownerId);
        } catch (...) {}
    }

    if (audioIdNum == 0) {
        if (callback) callback(false, "Invalid VK track ID: " + trackId);
        return;
    }

    QUrl url("https://api.vk.com/method/audio.delete");
    QUrlQuery q;
    q.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    q.addQueryItem("v", QString::fromStdString(m_apiVersion));
    q.addQueryItem("audio_id", QString::number(audioIdNum));
    q.addQueryItem("owner_id", QString::number(ownerIdNum));
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", kVkApiUserAgent);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        if (json.object().contains("response")) {
            if (callback) callback(true, "");
        } else {
            std::string err = json.object().contains("error")
                ? json.object()["error"].toObject()["error_msg"].toString().toStdString()
                : "Unknown error removing track";
            if (callback) callback(false, err);
        }
    }, [callback](const std::string& err) {
        if (callback) callback(false, err);
    });
}