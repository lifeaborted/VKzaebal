#include "VkApiClient.h"
#include "utils/logger/logger.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

VkApiClient::VkApiClient(QObject* parent) 
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "VkApiClient created.");
}

VkApiClient::~VkApiClient() {
    Logger::Log(LogLevel::INFO, "VkApiClient destroyed.");
}

void VkApiClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void VkApiClient::FetchUserAudio(long long ownerId, int count) {
    if (m_accessToken.empty()) {
        emit ApiError("Access token is missing!");
        return;
    }

    QUrl url("https://api.vk.com/method/audio.get");
    QUrlQuery query;
    if (ownerId != 0) {
        query.addQueryItem("owner_id", QString::number(ownerId));
    }
    query.addQueryItem("count", QString::number(count));
    query.addQueryItem("v", "5.199");
    query.addQueryItem("access_token", QString::fromStdString(m_accessToken));
    
    url.setQuery(query);
    QNetworkRequest request(url); //[cite: 4]

    // Маскируемся под Android-клиент
    request.setHeader(QNetworkRequest::UserAgentHeader, "KateMobileAndroid/56 lite-460 (Android 4.4.2; SDK 19; x86; unknown Android SDK built for x86; en)");

    Logger::Log(LogLevel::INFO, "VkApiClient: Fetching audio...");
    
    QNetworkReply* reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { OnReplyFinished(reply); });
}

void VkApiClient::OnReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        std::string err = reply->errorString().toStdString();
        Logger::Log(LogLevel::ERROR, "VkApiClient Network Error: " + err);
        emit ApiError(err);
        return;
    }

    QByteArray responseData = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);

    if (!jsonDoc.isObject()) {
        emit ApiError("Invalid JSON response");
        return;
    }

    QJsonObject rootObj = jsonDoc.object();
    
    // Проверка на ошибку от самого API VK
    if (rootObj.contains("error")) {
        QJsonObject errObj = rootObj["error"].toObject();
        std::string errMsg = errObj["error_msg"].toString().toStdString();
        Logger::Log(LogLevel::ERROR, "VK API Error: " + errMsg);
        emit ApiError(errMsg);
        return;
    }

    QJsonObject responseObj = rootObj["response"].toObject();
    QJsonArray itemsArray = responseObj["items"].toArray();

    std::vector<Track> tracks;
    for (int i = 0; i < itemsArray.size(); ++i) {
        QJsonObject trackObj = itemsArray[i].toObject();
        
        Track t;
        t.id = trackObj["id"].toInt();
        t.ownerId = trackObj["owner_id"].toInt();
        t.artist = trackObj["artist"].toString().toStdString();
        t.title = trackObj["title"].toString().toStdString();
        t.url = trackObj["url"].toString().toStdString();
        t.duration = trackObj["duration"].toInt();
        
        // VK иногда отдает пустые URL для заблокированных треков
        if (!t.url.empty()) {
            tracks.push_back(t);
        }
    }

    Logger::Log(LogLevel::INFO, "VkApiClient: Successfully parsed " + std::to_string(tracks.size()) + " tracks.");
    emit AudioFetched(tracks);
}