#include "BaseApiProvider.h"
#include "utils/logger/Logger.h"

#include <QThreadPool>
#include <QPointer>
#include <QCoreApplication>

BaseApiProvider::BaseApiProvider(QObject* parent)
    : IAudioProvider(parent), m_manager(new QNetworkAccessManager(this)) {
}

BaseApiProvider::~BaseApiProvider() {
}

void BaseApiProvider::SetAccessToken(const std::string& token) {
    m_accessToken = token;
}

void BaseApiProvider::SendJsonRequest(QNetworkRequest request, std::function<void(const QJsonDocument&)> onSuccess, std::function<void(const std::string&)> onFail, const QByteArray& postData) {

    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = postData.isEmpty() ? m_manager->get(request) : m_manager->post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess, onFail]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data = reply->readAll();

        if (reply->error() != QNetworkReply::NoError) {
            std::string err = reply->errorString().toStdString();
            Logger::Log(LogLevel::ERROR, "API Network Error (" + std::to_string(statusCode) + "): " + err);

            QJsonDocument json = QJsonDocument::fromJson(data);
            if (!json.isNull()) {
                HandleApiError(json, statusCode);
            } else if (statusCode == 401) {
                emit TokenExpired();
            }

            if (onFail) onFail(err);
            reply->deleteLater();
            return;
        }

        QPointer<BaseApiProvider> safeThis(this);
        QThreadPool::globalInstance()->start([safeThis, data, statusCode, onSuccess, onFail]() {
            QJsonDocument json = QJsonDocument::fromJson(data);

            QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, json, statusCode, onSuccess, onFail]() {
                if (!safeThis) return;

                if (json.isNull()) {
                    Logger::Log(LogLevel::ERROR, "API Error: Invalid JSON response");
                    if (onFail) onFail("Invalid JSON");
                } else {
                    if (!safeThis->HandleApiError(json, statusCode)) {
                        onSuccess(json);
                    } else {
                        if (onFail) onFail("API handled error");
                    }
                }
            }, Qt::QueuedConnection);
        });

        reply->deleteLater();
    });
}