#include "VkDeviceManager.h"
#include "utils/path/PathManager.h"
#include "utils/logger/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QRandomGenerator>

VkDeviceManager& VkDeviceManager::Instance() {
    static VkDeviceManager instance;
    return instance;
}

VkDeviceManager::VkDeviceManager() {
    m_configFilePath = PathManager::GetAppDataDir() + "/vk_device_profile.json";
    LoadOrGenerate();
}

QString VkDeviceManager::GetDeviceId() {
    if (m_deviceId.isEmpty()) {
        LoadOrGenerate();
    }
    return m_deviceId;
}

QString VkDeviceManager::GetAuthUserAgent() const {
    return "VKAndroidApp/8.183-54468 (Android 14; SDK 34; arm64-v8a; Google; Pixel 7; ru; 2400x1080)";
}

QString VkDeviceManager::GetAudioUserAgent() const {
    return "VKAndroidApp/8.96-22758 (Android 14; SDK 34; arm64-v8a; Google; Pixel 7; ru; 2400x1080)";
}

QString VkDeviceManager::GetGeneralUserAgent() const {
    return "VKAndroidApp/8.108-26257 (Android 14; SDK 34; arm64-v8a; Google; Pixel 7; ru; 2400x1080)";
}

QString VkDeviceManager::GetTrustedHash() const {
    return m_trustedHash;
}

void VkDeviceManager::SetTrustedHash(const QString& hash) {
    if (m_trustedHash != hash) {
        m_trustedHash = hash;
        Save();
        Logger::Log(LogLevel::INFO, "VkDeviceManager: Updated and saved trusted_hash.");
    }
}

void VkDeviceManager::LoadOrGenerate() {
    QFile file(m_configFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            m_deviceId = obj["device_id"].toString();
            m_trustedHash = obj["trusted_hash"].toString();
            if (!m_deviceId.isEmpty()) {
                Logger::Log(LogLevel::INFO, "VkDeviceManager: Loaded existing device_id: " + m_deviceId.toStdString());
                return;
            }
        }
    }

    // Generate compliant device_id: "%016llx:%s"
    quint64 random64 = QRandomGenerator::global()->generate64();
    QString hexPrefix = QString("%1").arg(random64, 16, 16, QChar('0'));
    QString uuidPart = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');

    m_deviceId = hexPrefix + ":" + uuidPart;
    Logger::Log(LogLevel::INFO, "VkDeviceManager: Generated new compliant device_id: " + m_deviceId.toStdString());
    Save();
}

void VkDeviceManager::Save() {
    QJsonObject obj;
    obj["device_id"] = m_deviceId;
    obj["trusted_hash"] = m_trustedHash;

    QFile file(m_configFilePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        file.close();
    } else {
        Logger::Log(LogLevel::ERROR, "VkDeviceManager: Failed to write config to " + m_configFilePath.toStdString());
    }
}
