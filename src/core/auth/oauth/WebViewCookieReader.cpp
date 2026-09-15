#include "WebViewCookieReader.h"
#include "utils/logger/Logger.h"

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QMap>
#include <QStringList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#include <wincrypt.h>
#include <bcrypt.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

std::string WebViewCookieReader::GetFullYouTubeCookies() {
#ifdef _WIN32
    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList possibleDirs = {
        localAppData + "/VKAudioTeam/VKAudioPlayer/WebView2/EBWebView",
        localAppData + "/VKAudioPlayer/WebView2/EBWebView"
    };

    QString baseDir;
    for (const auto& d : possibleDirs) {
        if (QFile::exists(d + "/Local State") && QFile::exists(d + "/Default/Network/Cookies")) {
            baseDir = d;
            break;
        }
    }

    if (baseDir.isEmpty()) {
        Logger::Log(LogLevel::WARNING, "WebViewCookieReader: WebView2 user data directory not found.");
        return "";
    }

    // 1. Извлекаем зашифрованный AES-ключ из Local State
    QFile localStateFile(baseDir + "/Local State");
    if (!localStateFile.open(QIODevice::ReadOnly)) {
        Logger::Log(LogLevel::ERROR, "WebViewCookieReader: Failed to open Local State");
        return "";
    }
    QJsonObject root = QJsonDocument::fromJson(localStateFile.readAll()).object();
    localStateFile.close();

    QString encKeyB64 = root["os_crypt"].toObject()["encrypted_key"].toString();
    if (encKeyB64.isEmpty()) {
        Logger::Log(LogLevel::ERROR, "WebViewCookieReader: No encrypted_key in Local State");
        return "";
    }

    QByteArray encKey = QByteArray::fromBase64(encKeyB64.toUtf8());
    if (!encKey.startsWith("DPAPI")) {
        Logger::Log(LogLevel::ERROR, "WebViewCookieReader: Encrypted key does not start with DPAPI");
        return "";
    }
    encKey = encKey.mid(5);

    // 2. Расшифровываем ключ через DPAPI
    DATA_BLOB inBlob;
    inBlob.cbData = static_cast<DWORD>(encKey.size());
    inBlob.pbData = reinterpret_cast<BYTE*>(encKey.data());
    DATA_BLOB outBlob;
    outBlob.cbData = 0;
    outBlob.pbData = nullptr;

    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        Logger::Log(LogLevel::ERROR, "WebViewCookieReader: CryptUnprotectData failed with error " + std::to_string(GetLastError()));
        return "";
    }

    QByteArray aesKey(reinterpret_cast<const char*>(outBlob.pbData), outBlob.cbData);
    LocalFree(outBlob.pbData);

    // 3. Инициализируем BCrypt AES-GCM
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (st != 0) {
        Logger::Log(LogLevel::ERROR, "WebViewCookieReader: BCryptOpenAlgorithmProvider failed.");
        return "";
    }

    st = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (st != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    st = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)aesKey.data(), aesKey.size(), 0);
    if (st != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // 4. Копируем базу данных Cookies во временный файл (во избежание блокировки процессом WebView)
    QString tempCookiesPath = QDir::tempPath() + "/vkaudio_temp_cookies.db";
    QFile::remove(tempCookiesPath);
    if (!QFile::copy(baseDir + "/Default/Network/Cookies", tempCookiesPath)) {
        Logger::Log(LogLevel::ERROR, "WebViewCookieReader: Failed to copy Cookies database to temp path.");
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // 5. Читаем и расшифровываем куки из SQLite
    QMap<QString, QString> cookieMap;
    {
        const QString connName = "webview_cookie_reader_conn";
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(tempCookiesPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare("SELECT name, encrypted_value FROM cookies WHERE host_key LIKE '%youtube.com'");
            if (query.exec()) {
                while (query.next()) {
                    QString name = query.value(0).toString();
                    QByteArray enc = query.value(1).toByteArray();
                    if (enc.startsWith("v10") || enc.startsWith("v11")) {
                        if (enc.size() > 3 + 12 + 16) {
                            QByteArray nonce = enc.mid(3, 12);
                            QByteArray ciphertext = enc.mid(15, enc.size() - 3 - 12 - 16);
                            QByteArray tag = enc.right(16);

                            BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
                            BCRYPT_INIT_AUTH_MODE_INFO(info);
                            info.pbNonce = (PUCHAR)nonce.data();
                            info.cbNonce = nonce.size();
                            info.pbTag = (PUCHAR)tag.data();
                            info.cbTag = tag.size();

                            QByteArray outBuf(ciphertext.size(), 0);
                            ULONG outLen = 0;
                            st = BCryptDecrypt(hKey, (PUCHAR)ciphertext.data(), ciphertext.size(), &info, nullptr, 0, (PUCHAR)outBuf.data(), outBuf.size(), &outLen, 0);
                            if (st == 0) {
                                outBuf.resize(outLen);
                                // Chromium 120+ добавляет 32-байтный заголовок перед чистым значением
                                if (outBuf.size() > 32) {
                                    outBuf = outBuf.mid(32);
                                }
                                cookieMap[name] = QString::fromUtf8(outBuf);
                            }
                        }
                    } else if (!enc.isEmpty()) {
                        cookieMap[name] = QString::fromUtf8(enc);
                    }
                }
            } else {
                Logger::Log(LogLevel::ERROR, "WebViewCookieReader: SQL query failed: " + query.lastError().text().toStdString());
            }
            db.close();
        } else {
            Logger::Log(LogLevel::ERROR, "WebViewCookieReader: Failed to open SQLite DB: " + db.lastError().text().toStdString());
        }
        QSqlDatabase::removeDatabase(connName);
    }

    QFile::remove(tempCookiesPath);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (cookieMap.isEmpty()) {
        Logger::Log(LogLevel::WARNING, "WebViewCookieReader: No YouTube cookies found in SQLite database.");
        return "";
    }

    QStringList pairs;
    for (auto it = cookieMap.begin(); it != cookieMap.end(); ++it) {
        pairs.append(it.key() + "=" + it.value());
    }

    std::string fullCookie = pairs.join("; ").toStdString();
    Logger::Log(LogLevel::INFO, "WebViewCookieReader: Successfully decrypted " + std::to_string(cookieMap.size()) +
                                " YouTube cookies from WebView2 (total length: " + std::to_string(fullCookie.size()) + ")");
    return fullCookie;
#else
    return "";
#endif
}
