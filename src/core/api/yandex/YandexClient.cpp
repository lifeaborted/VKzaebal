#include "YandexClient.h"
#include "utils/logger/Logger.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QXmlStreamReader>

YandexClient::YandexClient(QObject* parent) 
    : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
    Logger::Log(LogLevel::INFO, "YandexClient created.");
}

YandexClient::~YandexClient() {
    Logger::Log(LogLevel::INFO, "YandexClient destroyed.");
}

void YandexClient::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void YandexClient::FetchAllUserAudio(int offset, int count) {
    if (m_accessToken.empty()) {
        Logger::Log(LogLevel::ERROR, "Yandex: Token is empty!");
        emit FinishedFetching();
        return;
    }

    if (m_userId.empty()) {
        FetchUserId();
    } else {
        FetchLikesIds(offset, count);
    }
}

void YandexClient::FetchUserId() {
    QUrl url("https://api.music.yandex.net/account/status");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
            m_userId = std::to_string(json.object()["result"].toObject()["account"].toObject()["uid"].toInt());
            Logger::Log(LogLevel::INFO, "Yandex: Successfully got User ID: " + m_userId);

            FetchLikesIds(0, 200);
        } else {
            Logger::Log(LogLevel::ERROR, "Yandex API Error: Failed to fetch user status.");
            emit ApiError("Failed to fetch Yandex status");
        }
        reply->deleteLater();
    });
}

void YandexClient::FetchLikesIds(int offset, int count) {
    QUrl url(QString("https://api.music.yandex.net/users/%1/likes/tracks").arg(QString::fromStdString(m_userId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Yandex-Music-API");

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, offset, count]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument json = QJsonDocument::fromJson(responseData);
            QJsonArray tracksArray = json.object()["result"].toObject()["library"].toObject()["tracks"].toArray();

            Logger::Log(LogLevel::INFO, "Yandex: В лайках найдено треков: " + std::to_string(tracksArray.size()));

            QStringList chunkIds;
            for (int i = offset; i < offset + count && i < tracksArray.size(); ++i) {
                // Универсальный парсинг ID (Яндекс может отдавать их и строками, и числами)
                QJsonValue idVal = tracksArray[i].toObject()["id"];
                QString trackId = idVal.isString() ? idVal.toString() : QString::number(idVal.toInt());
                chunkIds.append(trackId);
            }

            if (!chunkIds.isEmpty()) {
                FetchTracksMetadata(chunkIds);

                if (offset + count < tracksArray.size()) {
                    FetchLikesIds(offset + count, count);
                }
            } else {
                Logger::Log(LogLevel::WARNING, "Yandex: Очередь ID пуста. Сырой ответ: " + responseData.left(200).toStdString());
                emit FinishedFetching();
            }
        } else {
            Logger::Log(LogLevel::ERROR, "Yandex API Error (FetchLikes): " + reply->errorString().toStdString());
            Logger::Log(LogLevel::ERROR, "Yandex API Body: " + reply->readAll().toStdString());
            emit FinishedFetching();
        }
        reply->deleteLater();
    });
}

void YandexClient::FetchTracksMetadata(const QStringList& trackIds) {
    QUrl url("https://api.music.yandex.net/tracks");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Yandex-Music-API");

    QByteArray postData = "track-ids=" + trackIds.join(",").toUtf8();
    QNetworkReply* reply = m_manager->post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
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
                track.url = "";

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

                if (!track.id.empty() && !track.title.empty()) {
                    chunkTracks.push_back(std::move(track));
                }
            }

            if (!chunkTracks.empty()) {
                Logger::Log(LogLevel::INFO, "Yandex: Fetched metadata for " + std::to_string(chunkTracks.size()) + " tracks.");
                emit AudioFetched(chunkTracks);
            } else {
                Logger::Log(LogLevel::WARNING, "Yandex: Metadata array is empty!");
            }
        } else {
            Logger::Log(LogLevel::ERROR, "Yandex API Error (FetchMeta): " + reply->errorString().toStdString());
            Logger::Log(LogLevel::ERROR, "Yandex API Body: " + reply->readAll().toStdString());
        }
        reply->deleteLater();
    });
}

void YandexClient::FetchTrackUrl(const std::string& trackId, std::function<void(const std::string&, bool)> callback) {
    QUrl url(QString("https://api.music.yandex.net/tracks/%1/download-info").arg(QString::fromStdString(trackId)));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));

    QNetworkReply* reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::ERROR, "Yandex API Error: Failed to fetch download info");
            callback("", true);
            reply->deleteLater();
            return;
        }

        QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
        QJsonArray result = json.object()["result"].toArray();

        QString downloadInfoUrl;
        for (const QJsonValue& val : result) {
            QJsonObject obj = val.toObject();
            if (obj["codec"].toString() == "mp3") {
                downloadInfoUrl = obj["downloadInfoUrl"].toString();
                break;
            }
        }

        if (downloadInfoUrl.isEmpty()) {
            Logger::Log(LogLevel::ERROR, "Yandex: No mp3 codec found for track");
            callback("", false);
            reply->deleteLater();
            return;
        }

        // Получаем XML-файл с нодами сервера и солью
        QNetworkRequest xmlRequest((QUrl(downloadInfoUrl)));
        xmlRequest.setRawHeader("Authorization", QByteArray("OAuth ") + QByteArray::fromStdString(m_accessToken));
        QNetworkReply* xmlReply = m_manager->get(xmlRequest);

        connect(xmlReply, &QNetworkReply::finished, this, [xmlReply, callback]() {
            if (xmlReply->error() != QNetworkReply::NoError) {
                Logger::Log(LogLevel::ERROR, "Yandex API Error: Failed to fetch XML node info");
                callback("", true);
                xmlReply->deleteLater();
                return;
            }

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

            if (host.isEmpty() || path.isEmpty() || ts.isEmpty() || s.isEmpty()) {
                Logger::Log(LogLevel::ERROR, "Yandex: Failed to parse XML node data");
                callback("", false);
                xmlReply->deleteLater();
                return;
            }

            // Формируем MD5 подпись
            QString magicStr = "XGRlBW9FXlekgbPrRHuAle";
            QString signData = magicStr + path.mid(1) + s;

            QByteArray hash = QCryptographicHash::hash(signData.toUtf8(), QCryptographicHash::Md5);
            QString sign = hash.toHex();
            QString finalUrl = "https://" + host + "/get-mp3/" + sign + "/" + ts + path;

            Logger::Log(LogLevel::INFO, "Yandex: Successfully generated track URL");
            callback(finalUrl.toStdString(), false);

            xmlReply->deleteLater();
        });

        reply->deleteLater();
    });
}