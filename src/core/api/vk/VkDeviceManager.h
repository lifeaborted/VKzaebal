#pragma once
#include <QString>
#include <string>

class VkDeviceManager {
public:
    static VkDeviceManager& Instance();

    // Returns a persistent, unique device_id formatted as "%016llx:%s"
    QString GetDeviceId();

    // User-Agent strings tailored to specific VK API operations
    QString GetAuthUserAgent() const;
    QString GetAudioUserAgent() const;
    QString GetGeneralUserAgent() const;

    // Trusted hash for skipping 2FA on known devices
    QString GetTrustedHash() const;
    void SetTrustedHash(const QString& hash);

    // Common client constants
    static constexpr const char* kClientId = "2274003";
    static constexpr const char* kClientSecret = "hHbZxrka2uZ6jB1inYsH";
    static constexpr const char* kApiVersionAuth = "5.272";
    static constexpr const char* kApiVersionAudio = "5.87";
    static constexpr const char* kApiVersionCatalog = "5.129";

private:
    VkDeviceManager();
    void LoadOrGenerate();
    void Save();

    QString m_deviceId;
    QString m_trustedHash;
    QString m_configFilePath;
};
