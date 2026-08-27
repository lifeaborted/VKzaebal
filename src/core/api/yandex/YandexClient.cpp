#include "YandexClient.h"
#include "utils/logger/Logger.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QXmlStreamReader>

YandexClient::YandexClient(QObject* parent) : BaseApiProvider(parent) {
    Logger::Log(LogLevel::INFO, "YandexClient created.");
}
YandexClient::~YandexClient() {
    Logger::Log(LogLevel::INFO, "YandexClient destroyed.");
}

bool YandexClient::HandleApiError(const QJsonDocument& json, int httpStatusCode) {
    QJsonObject root = json.object();
    if (root.contains("error")) {
        std::string errMsg = root["error"].isString() ? root["error"].toString().toStdString() : "Unknown Error";
        if (root["error"].isObject()) errMsg = root["error"].toObject()["message"].toString().toStdString();
        Logger::Log(LogLevel::ERROR, "Yandex API Error [" + std::to_string(httpStatusCode) + "]: " + errMsg);
        return true;
    }
    return false;
}

void YandexClient::FetchAllUserAudio(int offset, int count) {
    if (m_accessToken.empty()) { emit FinishedFetching(); return; }
    if (m_userId.empty()) FetchUserId();
    else FetchLikesIds(offset, count);
}

void YandexClient::FetchUserId() {
    QUrl url("https://api.music.yandex.net/account/status");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    SendJsonRequest(request, [this](const QJsonDocument& json) {
        m_userId = std::to_string(json.object()["result"].toObject()["account"].toObject()["uid"].toInt());
        Logger::Log(LogLevel::INFO, "Yandex: Successfully got User ID: " + m_userId);
        FetchLikesIds(0, 200);
    }, [this](const std::string&) { emit ApiError("Failed to fetch Yandex status"); });
}

void YandexClient::FetchLikesIds(int offset, int count) {
    QUrl url(QString("https://api.music.yandex.net/users/%1/likes/tracks").arg(QString::fromStdString(m_userId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Yandex-Music-API");

    SendJsonRequest(request, [this, offset, count](const QJsonDocument& json) {
        QJsonArray tracksArray = json.object()["result"].toObject()["library"].toObject()["tracks"].toArray();
        QStringList chunkIds;
        for (int i = offset; i < offset + count && i < tracksArray.size(); ++i) {
            QJsonValue idVal = tracksArray[i].toObject()["id"];
            chunkIds.append(idVal.isString() ? idVal.toString() : QString::number(idVal.toInt()));
        }
        if (!chunkIds.isEmpty()) {
            FetchTracksMetadata(chunkIds);
            if (offset + count < tracksArray.size()) FetchLikesIds(offset + count, count);
        } else emit FinishedFetching();
    }, [this](const std::string&) { emit FinishedFetching(); });
}

void YandexClient::FetchTracksMetadata(const QStringList& trackIds) {
    QUrl url("https://api.music.yandex.net/tracks");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Yandex-Music-API");

    QByteArray postData = "track-ids=" + trackIds.join(",").toUtf8();

    SendJsonRequest(request, [this](const QJsonDocument& json) {
        QJsonArray items = json.object()["result"].toArray();
        std::vector<Track> chunkTracks;
        chunkTracks.reserve(items.size());

        for (const QJsonValue& val : items) {
            QJsonObject trackObj = val.toObject();
            Track track;
            QJsonValue idVal = trackObj["id"];
            track.id = idVal.isString() ? idVal.toString().toStdString() : std::to_string(idVal.toInt());
            track.source = "Yandex";
            track.title = trackObj["title"].toString().toStdString();
            track.duration = trackObj["durationMs"].toInt() / 1000;
            if (trackObj.contains("artists") && trackObj["artists"].isArray()) {
                QJsonArray artistsArray = trackObj["artists"].toArray();
                QString artistName;
                for (int i = 0; i < artistsArray.size(); ++i) {
                    if (i > 0) artistName += ", ";
                    artistName += artistsArray[i].toObject()["name"].toString();
                }
                track.artist = artistName.toStdString();
            }
            QString coverUri = trackObj["coverUri"].toString();
            if (!coverUri.isEmpty()) {
                coverUri.replace("%%", "200x200");
                track.coverUrl = "https://" + coverUri.toStdString();
            }
            if (!track.id.empty() && !track.title.empty()) chunkTracks.push_back(std::move(track));
        }
        if (!chunkTracks.empty()) emit AudioFetched(chunkTracks);
    }, nullptr, postData);
}

void YandexClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QUrl url(QString("https://api.music.yandex.net/tracks/%1/download-info").arg(QString::fromStdString(trackId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    SendJsonRequest(request, [this, callback](const QJsonDocument& json) {
        QJsonArray result = json.object()["result"].toArray();
        QString downloadInfoUrl;
        for (const QJsonValue& val : result) {
            if (val.toObject()["codec"].toString() == "mp3") {
                downloadInfoUrl = val.toObject()["downloadInfoUrl"].toString();
                break;
            }
        }
        if (downloadInfoUrl.isEmpty()) { callback("", false); return; }

        QNetworkRequest xmlRequest((QUrl(downloadInfoUrl)));
        xmlRequest.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
        QNetworkReply* xmlReply = m_manager->get(xmlRequest);

        connect(xmlReply, &QNetworkReply::finished, this, [xmlReply, callback]() {
            if (xmlReply->error() != QNetworkReply::NoError) { callback("", true); xmlReply->deleteLater(); return; }
            QString host, path, ts, s;
            QXmlStreamReader xml(xmlReply->readAll());
            while (!xml.atEnd() && !xml.hasError()) {
                QXmlStreamReader::TokenType token = xml.readNext();
                if (token == QXmlStreamReader::StartElement) {
                    if (xml.name() == QString("host")) host = xml.readElementText();
                    else if (xml.name() == QString("path")) path = xml.readElementText();
                    else if (xml.name() == QString("ts")) ts = xml.readElementText();
                    else if (xml.name() == QString("s")) s = xml.readElementText();
                }
            }
            xmlReply->deleteLater();
            if (host.isEmpty() || path.isEmpty() || ts.isEmpty() || s.isEmpty()) { callback("", false); return; }

            QString signData = "XGRlBW9FXlekgbPrRHuAle" + path.mid(1) + s;
            QByteArray hash = QCryptographicHash::hash(signData.toUtf8(), QCryptographicHash::Md5);
            callback(("https://" + host + "/get-mp3/" + hash.toHex() + "/" + ts + path).toStdString(), false);
        });
    }, [callback](const std::string&) { callback("", true); });
}