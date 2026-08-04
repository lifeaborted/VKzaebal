#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <string>
#include <functional>

class LyricsFetcher : public QObject {
    Q_OBJECT
public:
    explicit LyricsFetcher(QObject* parent = nullptr);
    ~LyricsFetcher();

    void FetchLyrics(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback);

private:
    void SearchLrcLibFallback(const std::string& artist, const std::string& title, std::function<void(const std::string&)> callback);

    QNetworkAccessManager* m_manager;
};