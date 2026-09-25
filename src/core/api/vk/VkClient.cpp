#include "VkClient.h"
#include "VkDeviceManager.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>

VkClient::VkClient(QObject* parent, QNetworkAccessManager* manager) : BaseApiProvider(parent, manager) {
    Logger::Log(LogLevel::INFO, "VkClient created with VK Android API v5.87 specification.");
}

VkClient::~VkClient() {
    Logger::Log(LogLevel::INFO, "VkClient destroyed.");
}

void VkClient::SetSecret(const std::string& secret) {
    m_secret = secret;
    Logger::Log(LogLevel::INFO, "VkClient: Updated session secret for request signing.");
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

QString VkClient::CalculateSig(const std::string& method, const std::vector<std::pair<QString, QString>>& params) const {
    if (m_secret.empty()) return "";

    QString src = "/method/" + QString::fromStdString(method) + "?";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) src += "&";
        src += params[i].first + "=" + params[i].second;
    }
    src += QString::fromStdString(m_secret);

    return QString::fromUtf8(QCryptographicHash::hash(src.toUtf8(), QCryptographicHash::Md5).toHex()).toLower();
}

QNetworkRequest VkClient::BuildSignedPostRequest(const std::string& method,
                                                 const std::vector<std::pair<QString, QString>>& params,
                                                 QByteArray& outBody) const {
    QUrl url("https://api.vk.ru/method/" + QString::fromStdString(method));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", VkDeviceManager::Instance().GetAudioUserAgent().toUtf8());
    request.setRawHeader("x-vk-android-client", "new");
    request.setRawHeader("x-screen", "nowhere");

    QUrlQuery query;
    for (const auto& [k, v] : params) {
        query.addQueryItem(k, v);
    }

    if (!m_secret.empty()) {
        QString sig = CalculateSig(method, params);
        if (!sig.isEmpty()) {
            query.addQueryItem("sig", sig);
        }
    }

    outBody = query.query(QUrl::FullyEncoded).toUtf8();
    return request;
}

void VkClient::ValidateToken(std::function<void(bool)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false);
        return;
    }

    m_isValidatingToken = true;

    std::vector<std::pair<QString, QString>> params = {
        {"access_token", QString::fromStdString(m_accessToken)},
        {"v", QString::fromStdString(m_apiVersion)},
        {"fields", "deactivated"},
        {"lang", "ru"},
        {"https", "1"}
    };

    QByteArray body;
    QNetworkRequest request = BuildSignedPostRequest("users.get", params, body);

    SendJsonRequest(request, [this, callback](const QJsonDocument& json) {
        m_isValidatingToken = false;
        QJsonArray users = json.object()["response"].toArray();
        if (!users.isEmpty()) {
            QJsonObject u = users[0].toObject();
            if (u.contains("deactivated")) {
                std::string status = u["deactivated"].toString().toStdString();
                Logger::Log(LogLevel::ERROR, "VK: Account is deactivated/frozen: " + status);
                emit UserBlocked("VK", "User account is " + status);
                if (callback) callback(false);
                return;
            }
        }
        Logger::Log(LogLevel::INFO, "VkClient: Token validated successfully.");
        if (callback) callback(true);
    }, [this, callback](const std::string&) {
        m_isValidatingToken = false;
        if (callback) callback(false);
    }, body);
}

void VkClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    std::vector<std::pair<QString, QString>> params = {
        {"audios", QString::fromStdString(trackId)},
        {"access_token", QString::fromStdString(m_accessToken)},
        {"v", QString::fromStdString(m_apiVersion)},
        {"lang", "ru"},
        {"https", "1"}
    };

    QByteArray body;
    QNetworkRequest request = BuildSignedPostRequest("audio.getById", params, body);
    request.setTransferTimeout(5000);

    SendJsonRequest(request, [this, trackId, callback](const QJsonDocument& json) {
        std::string freshUrl = "";
        QJsonArray responseArray = json.object()["response"].toArray();
        if (!responseArray.isEmpty()) {
            freshUrl = responseArray[0].toObject()["url"].toString().toStdString();
        }

        // Check if URL is invalid/empty or points to unavailable audio stub
        if (freshUrl.empty() || freshUrl.find("audio_api_unavailable.mp3") != std::string::npos) {
            Logger::Log(LogLevel::WARNING, "VkClient: audio.getById returned empty/unavailable URL, attempting execute fallback...");
            
            // Execute fallback: return API.audio.getById({"audios":"..."})[0].url;
            std::vector<std::pair<QString, QString>> execParams = {
                {"code", QString("return API.audio.getById({\"audios\":\"%1\"})[0].url;").arg(QString::fromStdString(trackId))},
                {"access_token", QString::fromStdString(m_accessToken)},
                {"v", QString::fromStdString(m_apiVersion)},
                {"lang", "ru"},
                {"https", "1"}
            };
            QByteArray execBody;
            QNetworkRequest execReq = BuildSignedPostRequest("execute", execParams, execBody);
            execReq.setTransferTimeout(5000);

            SendJsonRequest(execReq, [callback](const QJsonDocument& execJson) {
                std::string fallbackUrl = execJson.object()["response"].toString().toStdString();
                if (callback) callback(fallbackUrl, false);
            }, [callback](const std::string&) {
                if (callback) callback("", true);
            }, execBody);
            return;
        }

        if (callback) callback(freshUrl, false);
    }, [callback](const std::string&) {
        if (callback) callback("", true);
    }, body);
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
    std::vector<std::pair<QString, QString>> params = {
        {"access_token", QString::fromStdString(m_accessToken)},
        {"v", QString::fromStdString(m_apiVersion)},
        {"offset", QString::number(offset)},
        {"count", QString::number(count)},
        {"lang", "ru"},
        {"https", "1"}
    };

    QByteArray body;
    QNetworkRequest request = BuildSignedPostRequest("audio.get", params, body);

    SendJsonRequest(request, [this, offset, count](const QJsonDocument& json) {
        QJsonArray items = json.object()["response"].toObject()["items"].toArray();
        std::vector<Track> chunkTracks = ParseVkTracks(items);

        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);

        if (items.size() == count) FetchAllUserAudio(offset + count, count);
        else emit FinishedFetching();

    }, [this](const std::string&) {
        emit FinishedFetching();
    }, body);
}

void VkClient::SearchAudio(const std::string& query, int count, int offset,
                           std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback({}, "VK access token is empty");
        return;
    }

    std::vector<std::pair<QString, QString>> params = {
        {"access_token", QString::fromStdString(m_accessToken)},
        {"v", QString::fromStdString(m_apiVersion)},
        {"q", QString::fromStdString(query)},
        {"count", QString::number(count)},
        {"offset", QString::number(offset)},
        {"auto_complete", "1"},
        {"sort", "2"},
        {"lang", "ru"},
        {"https", "1"}
    };

    QByteArray body;
    QNetworkRequest request = BuildSignedPostRequest("audio.search", params, body);
    request.setTransferTimeout(8000);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        QJsonArray items = json.object()["response"].toObject()["items"].toArray();
        std::vector<Track> tracks = ParseVkTracks(items);
        if (callback) callback(tracks, "");
    }, [callback](const std::string& err) {
        if (callback) callback({}, err);
    }, body);
}

void VkClient::AddTrackToFavorites(const std::string& trackId, const std::string& ownerId,
                                   std::function<void(bool, const std::string&)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false, "VK access token is empty");
        return;
    }

    std::string cleanAudioId = trackId;
    auto underscorePos = cleanAudioId.find('_');
    if (underscorePos != std::string::npos) {
        cleanAudioId = cleanAudioId.substr(underscorePos + 1);
        auto secondUnderscore = cleanAudioId.find('_');
        if (secondUnderscore != std::string::npos) {
            cleanAudioId = cleanAudioId.substr(0, secondUnderscore);
        }
    }

    std::vector<std::pair<QString, QString>> params = {
        {"audio_id", QString::fromStdString(cleanAudioId)},
        {"owner_id", QString::fromStdString(ownerId)},
        {"access_token", QString::fromStdString(m_accessToken)},
        {"v", QString::fromStdString(m_apiVersion)},
        {"lang", "ru"},
        {"https", "1"}
    };

    QByteArray body;
    QNetworkRequest request = BuildSignedPostRequest("audio.add", params, body);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        QJsonObject root = json.object();
        if (root.contains("error")) {
            std::string errMsg = root["error"].toObject()["error_msg"].toString().toStdString();
            if (callback) callback(false, errMsg);
        } else {
            if (callback) callback(true, "");
        }
    }, [callback](const std::string& err) {
        if (callback) callback(false, err);
    }, body);
}

void VkClient::RemoveTrackFromFavorites(const std::string& trackId, const std::string& ownerId,
                                        std::function<void(bool, const std::string&)> callback) {
    if (m_accessToken.empty()) {
        if (callback) callback(false, "VK access token is empty");
        return;
    }

    std::string cleanAudioId = trackId;
    auto underscorePos = cleanAudioId.find('_');
    if (underscorePos != std::string::npos) {
        cleanAudioId = cleanAudioId.substr(underscorePos + 1);
        auto secondUnderscore = cleanAudioId.find('_');
        if (secondUnderscore != std::string::npos) {
            cleanAudioId = cleanAudioId.substr(0, secondUnderscore);
        }
    }

    std::vector<std::pair<QString, QString>> params = {
        {"audio_id", QString::fromStdString(cleanAudioId)},
        {"owner_id", QString::fromStdString(ownerId)},
        {"access_token", QString::fromStdString(m_accessToken)},
        {"v", QString::fromStdString(m_apiVersion)},
        {"lang", "ru"},
        {"https", "1"}
    };

    QByteArray body;
    QNetworkRequest request = BuildSignedPostRequest("audio.delete", params, body);

    SendJsonRequest(request, [callback](const QJsonDocument& json) {
        QJsonObject root = json.object();
        if (root.contains("error")) {
            std::string errMsg = root["error"].toObject()["error_msg"].toString().toStdString();
            if (callback) callback(false, errMsg);
        } else {
            if (callback) callback(true, "");
        }
    }, [callback](const std::string& err) {
        if (callback) callback(false, err);
    }, body);
}