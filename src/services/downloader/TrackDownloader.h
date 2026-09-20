#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <string>
#include "../../models/Track.h"

class TrackDownloader : public QObject {
    Q_OBJECT
public:
    explicit TrackDownloader(QObject* parent = nullptr, QNetworkAccessManager* manager = nullptr);
    void Download(const Track& track, const std::string& url, const QString& customDir = QString());

private:
    QNetworkAccessManager* m_manager = nullptr;
};