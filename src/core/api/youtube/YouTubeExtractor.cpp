#include "YouTubeExtractor.h"
#include "YouTubePoTokenGenerator.h"
#include "utils/logger/Logger.h"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

YouTubeExtractor::YouTubeExtractor(QNetworkAccessManager* networkManager, YouTubePoTokenGenerator* tokenGen, QObject* parent)
    : QObject(parent), m_manager(networkManager), m_tokenGen(tokenGen) {
}

void YouTubeExtractor::extractAudioUrl(const QString& videoId, std::function<void(const QString&, bool)> callback) {
    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Starting audio extraction for video: " + videoId.toStdString());

    // Шаг 1: Получаем base.js и visitorData
    fetchBaseJs(videoId, [this, videoId, callback](const QString& baseJs, const QString& visitorData) {
        // Шаг 2: В первую очередь запрашиваем через VisionOS клиент (отдает прямые потоки без BotGuard)
        sendVisionOsRequest(videoId, visitorData, baseJs, [this, videoId, visitorData, baseJs, callback](const QString& url, bool isError) {
            if (!isError && !url.isEmpty()) {
                callback(url, false);
                return;
            }

            Logger::Log(LogLevel::WARNING, "YouTubeExtractor: VisionOS request failed or unplayable. Falling back to Web client with PoToken...");

            // Шаг 3: Фолбэк на Web клиент с генерацией poToken
            if (m_tokenGen) {
                m_tokenGen->generateToken("", [this, videoId, visitorData, baseJs, callback](QString poToken, QString genVisitorData) {
                    QString effVisitor = !visitorData.isEmpty() ? visitorData : genVisitorData;
                    sendWebRequest(videoId, poToken, effVisitor, baseJs, callback);
                });
            } else {
                sendWebRequest(videoId, "", visitorData, baseJs, callback);
            }
        });
    });
}

/**
 * @brief Загружает embed страницу для поиска base.js и visitorData, затем кэширует base.js.
 */
void YouTubeExtractor::fetchBaseJs(const QString& videoId, std::function<void(const QString&, const QString&)> callback) {
    if (!m_cachedBaseJs.isEmpty() && !m_cachedVisitorData.isEmpty()) {
        callback(m_cachedBaseJs, m_cachedVisitorData);
        return;
    }

    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Resolving player base.js & visitorData from embed page...");
    QUrl embedUrl(QString("https://www.youtube.com/embed/%1").arg(videoId));
    QNetworkRequest req(embedUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Failed to fetch embed page: " + reply->errorString().toStdString());
            callback(m_cachedBaseJs, m_cachedVisitorData);
            return;
        }

        QString html = QString::fromUtf8(reply->readAll());

        // 1. Поиск visitorData
        QRegularExpression visRe(R"raw((?:VISITOR_DATA|"visitorData")\s*[:=]\s*"([^"]+)")raw");
        QRegularExpressionMatch visMatch = visRe.match(html);
        if (visMatch.hasMatch()) {
            m_cachedVisitorData = visMatch.captured(1);
            Logger::Log(LogLevel::INFO, "YouTubeExtractor: Found visitorData in embed page: " + m_cachedVisitorData.toStdString());
        }

        // 2. Поиск пути к base.js
        QRegularExpression jsRe(R"raw((/s/player/[^"']+base\.js))raw");
        QRegularExpressionMatch jsMatch = jsRe.match(html);
        if (!jsMatch.hasMatch()) {
            Logger::Log(LogLevel::WARNING, "YouTubeExtractor: base.js regex match failed in embed page.");
            callback(m_cachedBaseJs, m_cachedVisitorData);
            return;
        }

        QString jsPath = jsMatch.captured(1).replace("\\/", "/");
        m_baseJsUrl = "https://www.youtube.com" + jsPath;
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Found base.js URL: " + m_baseJsUrl.toStdString());

        // 3. Скачиваем base.js, если еще не в кэше
        if (!m_cachedBaseJs.isEmpty()) {
            callback(m_cachedBaseJs, m_cachedVisitorData);
            return;
        }

        QNetworkRequest jsReq((QUrl(m_baseJsUrl)));
        jsReq.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36");
        jsReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply* jsReply = m_manager->get(jsReq);
        connect(jsReply, &QNetworkReply::finished, this, [this, jsReply, callback]() {
            jsReply->deleteLater();
            if (jsReply->error() == QNetworkReply::NoError) {
                m_cachedBaseJs = QString::fromUtf8(jsReply->readAll());
                Logger::Log(LogLevel::INFO, "YouTubeExtractor: Successfully cached base.js (" + std::to_string(m_cachedBaseJs.size()) + " bytes)");
            } else {
                Logger::Log(LogLevel::ERROR, "YouTubeExtractor: Failed to download base.js: " + jsReply->errorString().toStdString());
            }
            callback(m_cachedBaseJs, m_cachedVisitorData);
        });
    });
}

/**
 * @brief Отправляет запрос к InnerTube API с профилем VisionOS.
 * VisionOS клиент не требует BotGuard проверки, возвращая прямые потоки (itag 140, 251).
 */
void YouTubeExtractor::sendVisionOsRequest(const QString& videoId, const QString& visitorData, const QString& baseJs, std::function<void(const QString&, bool)> callback) {
    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Sending VisionOS client request for videoId=" + videoId.toStdString());

    QJsonObject clientObj;
    clientObj["clientName"] = "VISIONOS";
    clientObj["clientVersion"] = "1.02";
    clientObj["deviceMake"] = "Apple";
    clientObj["deviceModel"] = "RealityDevice17,1";
    clientObj["userAgent"] = "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15";
    clientObj["osName"] = "visionOS";
    clientObj["osVersion"] = "26.5.23O471";
    clientObj["hl"] = "en";
    clientObj["timeZone"] = "UTC";
    clientObj["utcOffsetMinutes"] = 0;

    QJsonObject contextObj;
    contextObj["client"] = clientObj;

    QJsonObject contentPlayback;
    contentPlayback["html5Preference"] = "HTML5_PREF_WANTS";
    contentPlayback["signatureTimestamp"] = 20702;
    QJsonObject playbackContext;
    playbackContext["contentPlaybackContext"] = contentPlayback;

    QJsonObject requestObj;
    requestObj["context"] = contextObj;
    requestObj["videoId"] = videoId;
    requestObj["playbackContext"] = playbackContext;
    requestObj["contentCheckOk"] = true;
    requestObj["racyCheckOk"] = true;

    QNetworkRequest request(QUrl("https://www.youtube.com/youtubei/v1/player?prettyPrint=false"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Youtube-Client-Name", "101");
    request.setRawHeader("X-Youtube-Client-Version", "1.02");
    request.setRawHeader("Origin", "https://www.youtube.com");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15");
    if (!visitorData.isEmpty()) {
        request.setRawHeader("X-Goog-Visitor-Id", visitorData.toUtf8());
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QByteArray payload = QJsonDocument(requestObj).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_manager->post(request, payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId, baseJs, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Logger::Log(LogLevel::WARNING, "YouTubeExtractor: VisionOS API Network Error: " + reply->errorString().toStdString());
            callback("", true);
            return;
        }

        QByteArray responseData = reply->readAll();
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: VisionOS response received (" + std::to_string(responseData.size()) + " bytes)");

        QJsonParseError jsonErr;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &jsonErr);
        if (jsonErr.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
            Logger::Log(LogLevel::WARNING, "YouTubeExtractor: VisionOS Invalid JSON: " + jsonErr.errorString().toStdString());
            callback("", true);
            return;
        }

        bool parsed = parsePlayerResponse(jsonDoc.object(), videoId, baseJs, callback);
        if (!parsed) {
            callback("", true);
        }
    });
}

/**
 * @brief Отправляет POST запрос к https://www.youtube.com/youtubei/v1/player с Web клиентом.
 */
void YouTubeExtractor::sendWebRequest(const QString& videoId, const QString& poToken, const QString& visitorData, const QString& baseJs, std::function<void(const QString&, bool)> callback) {
    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Sending Web player API request for videoId=" + videoId.toStdString());

    QJsonObject clientObj;
    clientObj["hl"] = "en";
    clientObj["gl"] = "US";
    clientObj["clientName"] = "WEB";
    clientObj["clientVersion"] = "2.20250101.00.00";
    if (!visitorData.isEmpty()) {
        clientObj["visitorData"] = visitorData;
    }
    if (!poToken.isEmpty()) {
        QJsonObject integrityObj;
        integrityObj["poToken"] = poToken;
        clientObj["serviceIntegrityDimensions"] = integrityObj;
    }
    clientObj["originalUrl"] = QString("https://www.youtube.com/watch?v=%1").arg(videoId);

    QJsonObject contextObj;
    contextObj["client"] = clientObj;

    QJsonObject contentPlayback;
    contentPlayback["html5Preference"] = "HTML5_PREF_WANTS";
    QJsonObject playbackContext;
    playbackContext["contentPlaybackContext"] = contentPlayback;

    QJsonObject requestObj;
    requestObj["context"] = contextObj;
    requestObj["videoId"] = videoId;
    requestObj["playbackContext"] = playbackContext;
    requestObj["contentCheckOk"] = true;
    requestObj["racyCheckOk"] = true;

    QNetworkRequest request(QUrl("https://www.youtube.com/youtubei/v1/player?prettyPrint=false"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36");
    request.setRawHeader("Origin", "https://www.youtube.com");
    request.setRawHeader("Referer", "https://www.youtube.com/");
    request.setRawHeader("X-YouTube-Client-Name", "1");
    request.setRawHeader("X-YouTube-Client-Version", "2.20250101.00.00");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QByteArray payload = QJsonDocument(requestObj).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = m_manager->post(request, payload);

    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId, baseJs, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString err = "YouTubeExtractor: Web Player API Network Error: " + reply->errorString();
            Logger::Log(LogLevel::ERROR, err.toStdString());
            emit extractionFailed(videoId, err);
            callback("", true);
            return;
        }

        QByteArray responseData = reply->readAll();
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Web server response (" + std::to_string(responseData.size()) + " bytes):\n" + responseData.toStdString());

        QJsonParseError jsonErr;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &jsonErr);
        if (jsonErr.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
            QString err = "YouTubeExtractor: Web Invalid JSON: " + jsonErr.errorString();
            Logger::Log(LogLevel::ERROR, err.toStdString());
            emit extractionFailed(videoId, err);
            callback("", true);
            return;
        }

        bool parsed = parsePlayerResponse(jsonDoc.object(), videoId, baseJs, callback);
        if (!parsed) {
            callback("", true);
        }
    });
}

/**
 * @brief Парсинг JSON-ответа плеера: проверка playabilityStatus, извлечение аудиоформатов,
 * расшифровка signatureCipher и декодирование n-token.
 * @return true если формат успешно извлечен и передан в callback, false иначе.
 */
bool YouTubeExtractor::parsePlayerResponse(const QJsonObject& root, const QString& videoId, const QString& baseJs, std::function<void(const QString&, bool)> callback) {
    // 1. Проверка статуса воспроизведения
    if (root.contains("playabilityStatus")) {
        QJsonObject pStatus = root["playabilityStatus"].toObject();
        QString status = pStatus["status"].toString();
        if (status != "OK") {
            QString reason = pStatus["reason"].toString();
            QString errMsg = QString("YouTubeExtractor: Video unplayable (status: %1, reason: %2)").arg(status, reason);
            Logger::Log(LogLevel::WARNING, errMsg.toStdString());
            return false;
        }
    }

    // 2. Извлечение streamingData
    if (!root.contains("streamingData")) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Response does not contain 'streamingData'!");
        return false;
    }

    QJsonObject streamingData = root["streamingData"].toObject();

    // 3. Приоритет 1: HLS манифест (AAC в ADTS/TS контейнере)
    if (streamingData.contains("hlsManifestUrl")) {
        QString hlsUrl = streamingData["hlsManifestUrl"].toString();
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Found HLS master manifest: " + hlsUrl.left(90).toStdString() + "...");

        QNetworkRequest req((QUrl(hlsUrl)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* hlsReply = m_manager->get(req);

        connect(hlsReply, &QNetworkReply::finished, this, [this, hlsReply, videoId, root, baseJs, callback]() {
            hlsReply->deleteLater();
            if (hlsReply->error() == QNetworkReply::NoError) {
                QString manifest = QString::fromUtf8(hlsReply->readAll());
                QString audioUrl = extractAudioUrlFromMasterManifest(manifest);
                if (!audioUrl.isEmpty()) {
                    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Resolved HLS AAC audio stream: " + audioUrl.left(90).toStdString() + "...");
                    emit extractionFinished(videoId, audioUrl);
                    callback(audioUrl, false);
                    return;
                }
            }
            Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Failed to resolve audio from HLS manifest, falling back to direct formats.");
            if (!parseDirectFormats(root, videoId, baseJs, callback)) {
                callback("", true);
            }
        });
        return true;
    }

    return parseDirectFormats(root, videoId, baseJs, callback);
}

/**
 * @brief Извлекает прямую ссылку на аудио sub-manifest из мастер-манифеста HLS.
 * Предпочитает itag 234 (AAC-LC ~128 kbps), затем itag 233 (HE-AAC ~64 kbps) или любой другой аудио-поток.
 */
QString YouTubeExtractor::extractAudioUrlFromMasterManifest(const QString& manifest) {
    QString fallbackUrl;

    const QStringList lines = manifest.split(QLatin1Char('\n'));
    QRegularExpression uriRegex("URI=\"([^\"]+)\"");
    QRegularExpression groupRegex("GROUP-ID=\"([^\"]+)\"");

    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (!line.startsWith("#EXT-X-MEDIA:")) continue;
        if (!line.contains("TYPE=AUDIO")) continue;

        auto uriMatch = uriRegex.match(line);
        if (!uriMatch.hasMatch()) continue;

        QString uri = uriMatch.captured(1);
        auto groupMatch = groupRegex.match(line);
        QString groupId = groupMatch.hasMatch() ? groupMatch.captured(1) : "";

        // itag 234 - AAC-LC ~128 kbps (наивысший приоритет)
        if (groupId == "234" || uri.contains("/itag/234/")) {
            return uri;
        }

        // itag 233 - HE-AAC ~64 kbps или любой другой аудио-поток
        if (fallbackUrl.isEmpty()) {
            fallbackUrl = uri;
        }
    }

    return fallbackUrl;
}

/**
 * @brief Разбор прямых форматов из adaptiveFormats / formats (резервный путь).
 */
bool YouTubeExtractor::parseDirectFormats(const QJsonObject& root, const QString& videoId, const QString& baseJs, std::function<void(const QString&, bool)> callback) {
    QJsonObject streamingData = root["streamingData"].toObject();
    QJsonArray adaptiveFormats = streamingData["adaptiveFormats"].toArray();
    QJsonArray formats = streamingData["formats"].toArray();

    QJsonObject selectedFormat;
    int selectedItag = 0;

    // Приоритет 1: itag 140 (AAC audio 128 kbps) - оптимальный формат для MiniaudioEngine
    for (const QJsonValue& val : adaptiveFormats) {
        QJsonObject f = val.toObject();
        if (f["itag"].toInt() == 140) {
            selectedFormat = f;
            selectedItag = 140;
            break;
        }
    }

    // Приоритет 2: itag 251 (Opus audio 160 kbps)
    if (selectedFormat.isEmpty()) {
        for (const QJsonValue& val : adaptiveFormats) {
            QJsonObject f = val.toObject();
            if (f["itag"].toInt() == 251) {
                selectedFormat = f;
                selectedItag = 251;
                break;
            }
        }
    }

    // Приоритет 3: любой другой аудиопоток из adaptiveFormats
    if (selectedFormat.isEmpty()) {
        for (const QJsonValue& val : adaptiveFormats) {
            QJsonObject f = val.toObject();
            QString mime = f["mimeType"].toString();
            if (mime.startsWith("audio/")) {
                selectedFormat = f;
                selectedItag = f["itag"].toInt();
                break;
            }
        }
    }

    // Приоритет 4: itag 18 (MP4 360p с AAC аудио) из formats для кросс-линка
    if (selectedFormat.isEmpty()) {
        for (const QJsonValue& val : formats) {
            QJsonObject f = val.toObject();
            if (f["itag"].toInt() == 18) {
                selectedFormat = f;
                selectedItag = 18;
                break;
            }
        }
    }

    // Приоритет 5: первый доступный формат
    if (selectedFormat.isEmpty() && !formats.isEmpty()) {
        selectedFormat = formats.first().toObject();
        selectedItag = selectedFormat["itag"].toInt();
    }

    if (selectedFormat.isEmpty()) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: No audio or video formats found in streamingData!");
        return false;
    }

    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Selected format itag=" + std::to_string(selectedItag) + ", mime=" + selectedFormat["mimeType"].toString().toStdString());

    // 3. Извлечение базовой ссылки или signatureCipher
    QString streamUrl;
    if (selectedFormat.contains("url")) {
        streamUrl = selectedFormat["url"].toString();
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Format contains direct stream URL.");
    } else if (selectedFormat.contains("signatureCipher") || selectedFormat.contains("cipher")) {
        QString cipher = selectedFormat.contains("signatureCipher")
                             ? selectedFormat["signatureCipher"].toString()
                             : selectedFormat["cipher"].toString();
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Stream URL is ciphered. Decrypting signature via C++ AST decoder...");
        streamUrl = decryptSignature(cipher, baseJs);
    }

    if (streamUrl.isEmpty()) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Failed to resolve stream URL from selected format!");
        return false;
    }

    // 4. Проверка и трансформация n-token параметра (защита от троттлинга YouTube)
    QUrl parsedUrl(streamUrl);
    QUrlQuery query(parsedUrl);
    if (query.hasQueryItem("n")) {
        QString rawN = query.queryItemValue("n", QUrl::FullyDecoded);
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Detected n-token parameter: " + rawN.toStdString());
        QString transformedN = transformNToken(rawN, baseJs);
        if (!transformedN.isEmpty() && transformedN != rawN) {
            Logger::Log(LogLevel::INFO, "YouTubeExtractor: Successfully transformed n-token: " + transformedN.toStdString());
            query.removeQueryItem("n");
            query.addQueryItem("n", transformedN);
            parsedUrl.setQuery(query);
            streamUrl = parsedUrl.toString();
        } else {
            Logger::Log(LogLevel::INFO, "YouTubeExtractor: n-token unchanged, using raw.");
        }
    }

    // 5. Кросс-линк если использовался комбинированный itag 18
    if (selectedItag == 18) {
        streamUrl.replace(QRegularExpression("&itag=\\d+"), "&itag=140");
        streamUrl.replace("mime=video", "mime=audio");
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Applied itag 18 cross-link substitution to audio/140.");
    }

    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Final resolved stream URL: " + streamUrl.left(90).toStdString() + "...");
    emit extractionFinished(videoId, streamUrl);
    callback(streamUrl, false);
    return true;
}

/**
 * @brief Расшифровка signatureCipher на C++ (аналог yt-dlp AST парсера).
 */
QString YouTubeExtractor::decryptSignature(const QString& signatureCipher, const QString& baseJs) {
    QUrlQuery query(signatureCipher);
    QString s = query.queryItemValue("s", QUrl::FullyDecoded);
    QString sp = query.queryItemValue("sp", QUrl::FullyDecoded);
    QString rawUrl = query.queryItemValue("url", QUrl::FullyDecoded);
    if (sp.isEmpty()) sp = "sig";

    if (s.isEmpty() || rawUrl.isEmpty()) {
        Logger::Log(LogLevel::ERROR, "YouTubeExtractor: Invalid signatureCipher query components!");
        return "";
    }

    // 1. Попытка чистого C++ разбора операций из base.js
    QString objName;
    QList<CipherOperation> operations = parseCipherOperations(baseJs, objName);
    if (!operations.isEmpty()) {
        QString decryptedSig = executeCipherOperations(s, operations);
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Signature deciphered via pure C++ AST engine.");
        return rawUrl + "&" + sp + "=" + QString::fromUtf8(QUrl::toPercentEncoding(decryptedSig));
    }

    // 2. Резервный расчет через QJSEngine
    Logger::Log(LogLevel::WARNING, "YouTubeExtractor: C++ parser failed, falling back to QJSEngine evaluator...");
    QString decryptedSig = evaluateSignatureInJs(s, baseJs);
    if (!decryptedSig.isEmpty()) {
        Logger::Log(LogLevel::INFO, "YouTubeExtractor: Signature deciphered via QJSEngine fallback.");
        return rawUrl + "&" + sp + "=" + QString::fromUtf8(QUrl::toPercentEncoding(decryptedSig));
    }

    Logger::Log(LogLevel::ERROR, "YouTubeExtractor: Both C++ and JS decipherers failed! Returning raw URL with untouched signature.");
    return rawUrl + "&" + sp + "=" + QString::fromUtf8(QUrl::toPercentEncoding(s));
}

/**
 * @brief Парсинг операций расшифровки сигнатуры из base.js (аналог yt-dlp).
 */
QList<YouTubeExtractor::CipherOperation> YouTubeExtractor::parseCipherOperations(const QString& baseJs, QString& outObjName) {
    QList<CipherOperation> operations;
    if (baseJs.isEmpty()) return operations;

    QString funcBody;

    // Шаг 1: Поиск имени функции расшифровки сигнатуры по шаблонам yt-dlp
    QRegularExpression sigFuncRe(R"raw(\b[a-zA-Z0-9_$]+\s*&&\s*[a-zA-Z0-9_$]+\.set\([^,]+\s*,\s*encodeURIComponent\s*\(\s*([a-zA-Z0-9_$]+)\()raw");
    QRegularExpressionMatch sigMatch = sigFuncRe.match(baseJs);
    QString funcName;
    if (sigMatch.hasMatch()) {
        funcName = sigMatch.captured(1);
    } else {
        QRegularExpression sigFuncRe2(R"raw(([a-zA-Z0-9_$]+)\s*=\s*function\(\s*a\s*\)\s*\{\s*a\s*=\s*a\.split\(\s*""\s*\))raw");
        QRegularExpressionMatch sigMatch2 = sigFuncRe2.match(baseJs);
        if (sigMatch2.hasMatch()) {
            funcName = sigMatch2.captured(1);
        }
    }

    // Извлечение тела функции через подсчет фигурных скобок {}
    if (!funcName.isEmpty()) {
        QRegularExpression defRe(QString(R"raw((?:var\s+|let\s+|const\s+)?%1\s*=\s*function\s*\(([^)]*)\)\s*\{)raw").arg(QRegularExpression::escape(funcName)));
        QRegularExpressionMatch defMatch = defRe.match(baseJs);
        if (defMatch.hasMatch()) {
            int bodyStart = defMatch.capturedEnd(0) - 1;
            int open = 0, bodyEnd = -1;
            for (int i = bodyStart; i < baseJs.length(); ++i) {
                if (baseJs[i] == '{') open++;
                else if (baseJs[i] == '}') {
                    open--;
                    if (open == 0) { bodyEnd = i; break; }
                }
            }
            if (bodyEnd != -1) {
                funcBody = baseJs.mid(bodyStart + 1, bodyEnd - bodyStart - 1);
            }
        }
    }

    // Если функция не найдена по имени, используем AST поиск по .split("") и .join("")
    if (funcBody.isEmpty()) {
        int splitIdx = baseJs.indexOf(".split(\"\"");
        if (splitIdx == -1) splitIdx = baseJs.indexOf(".split('')");
        while (splitIdx != -1) {
            int joinIdx = baseJs.indexOf(".join(\"\"", splitIdx);
            if (joinIdx == -1) joinIdx = baseJs.indexOf(".join('')", splitIdx);
            if (joinIdx != -1 && (joinIdx - splitIdx) < 600) {
                int braceStart = baseJs.lastIndexOf('{', splitIdx);
                if (braceStart != -1) {
                    int open = 0, bodyEnd = -1;
                    for (int i = braceStart; i < baseJs.length(); ++i) {
                        if (baseJs[i] == '{') open++;
                        else if (baseJs[i] == '}') {
                            open--;
                            if (open == 0) { bodyEnd = i; break; }
                        }
                    }
                    if (bodyEnd != -1) {
                        funcBody = baseJs.mid(braceStart + 1, bodyEnd - braceStart - 1);
                        break;
                    }
                }
            }
            splitIdx = baseJs.indexOf(".split(", splitIdx + 1);
        }
    }

    if (funcBody.isEmpty()) {
        return operations;
    }

    // Шаг 2: Ищем вызовы методов вспомогательного объекта (например: wAa.bH(a, 3))
    QRegularExpression callRe(R"raw(([a-zA-Z0-9_$]+)\.([a-zA-Z0-9_$]+)\s*\(\s*[a-zA-Z0-9_$]+\s*(?:,\s*(\d+))?\s*\))raw");
    QRegularExpressionMatch callMatch = callRe.match(funcBody);
    if (!callMatch.hasMatch()) {
        return operations;
    }

    outObjName = callMatch.captured(1);

    // Шаг 3: Извлекаем тело объекта из base.js: var wAa = { ... };
    QString objPattern = QString(R"raw((?:var\s+|let\s+|const\s+)?%1\s*=\s*\{([\s\S]*?)\};)raw").arg(QRegularExpression::escape(outObjName));
    QRegularExpression objRe(objPattern);
    QRegularExpressionMatch objMatch = objRe.match(baseJs);
    if (!objMatch.hasMatch()) {
        return operations;
    }

    QString objBody = objMatch.captured(1);

    // Шаг 4: Определяем тип каждой операции (reverse, slice/splice, swap)
    QMap<QString, CipherOperation::Type> methodTypes;
    QRegularExpression methodDefRe(R"raw(([a-zA-Z0-9_$]+)\s*:\s*function\s*\([^)]*\)\s*\{([^}]+)\})raw");
    auto methodIter = methodDefRe.globalMatch(objBody);
    while (methodIter.hasNext()) {
        auto m = methodIter.next();
        QString methodName = m.captured(1);
        QString mBody = m.captured(2);

        if (mBody.contains("reverse")) {
            methodTypes[methodName] = CipherOperation::REVERSE;
        } else if (mBody.contains("splice") || mBody.contains("slice")) {
            methodTypes[methodName] = CipherOperation::SLICE;
        } else if (mBody.contains("%") || mBody.contains("[0]=") || mBody.contains("[0]") || mBody.contains("c=a[0]")) {
            methodTypes[methodName] = CipherOperation::SWAP;
        }
    }

    // Шаг 5: Формируем список операций в порядке их вызова
    auto callIter = callRe.globalMatch(funcBody);
    while (callIter.hasNext()) {
        auto m = callIter.next();
        if (m.captured(1) == outObjName) {
            QString methodName = m.captured(2);
            int arg = m.captured(3).toInt();

            if (methodTypes.contains(methodName)) {
                operations.append({ methodTypes[methodName], arg });
            }
        }
    }

    Logger::Log(LogLevel::INFO, "YouTubeExtractor: Extracted " + std::to_string(operations.size()) + " cipher AST operations for object " + outObjName.toStdString());
    return operations;
}

/**
 * @brief Применение разобранных операций на C++ к строке сигнатуры.
 */
QString YouTubeExtractor::executeCipherOperations(QString s, const QList<CipherOperation>& ops) {
    for (const auto& op : ops) {
        if (s.isEmpty()) break;
        switch (op.type) {
            case CipherOperation::REVERSE:
                std::reverse(s.begin(), s.end());
                break;
            case CipherOperation::SLICE:
                if (op.arg > 0 && op.arg < s.length()) {
                    s = s.mid(op.arg);
                }
                break;
            case CipherOperation::SWAP:
                if (!s.isEmpty() && op.arg > 0) {
                    int idx = op.arg % s.length();
                    std::swap(s[0], s[idx]);
                }
                break;
        }
    }
    return s;
}

/**
 * @brief Резервный вычислитель сигнатуры через QJSEngine.
 */
QString YouTubeExtractor::evaluateSignatureInJs(const QString& s, const QString& baseJs) {
    if (s.isEmpty() || baseJs.isEmpty()) return "";

    QString objName;
    int splitIdx = baseJs.indexOf(".split(\"\"");
    if (splitIdx == -1) splitIdx = baseJs.indexOf(".split('')");
    while (splitIdx != -1) {
        int joinIdx = baseJs.indexOf(".join(\"\"", splitIdx);
        if (joinIdx == -1) joinIdx = baseJs.indexOf(".join('')", splitIdx);
        if (joinIdx != -1 && (joinIdx - splitIdx) < 600) {
            int paramEnd = baseJs.lastIndexOf(')', splitIdx);
            int paramStart = baseJs.lastIndexOf('(', paramEnd);
            if (paramStart != -1 && paramEnd != -1) {
                QString paramName = baseJs.mid(paramStart + 1, paramEnd - paramStart - 1).trimmed();
                int bodyStart = baseJs.indexOf('{', paramEnd);
                if (bodyStart != -1) {
                    int open = 0, bodyEnd = -1;
                    for (int i = bodyStart; i < baseJs.length(); ++i) {
                        if (baseJs[i] == '{') open++;
                        else if (baseJs[i] == '}') {
                            open--;
                            if (open == 0) { bodyEnd = i; break; }
                        }
                    }
                    if (bodyEnd != -1) {
                        QString funcBody = baseJs.mid(bodyStart + 1, bodyEnd - bodyStart - 1);
                        QRegularExpression objRe(R"raw(([a-zA-Z0-9_$]+)\.[a-zA-Z0-9_$]+\(\s*[a-zA-Z0-9_$]+\s*,\s*\d+\s*\))raw");
                        auto objMatch = objRe.match(funcBody);
                        if (objMatch.hasMatch() && objMatch.captured(1) != paramName) {
                            objName = objMatch.captured(1);
                            QString objPattern = QString(R"raw((?:var|let|const)?\s*%1\s*=\s*\{([\s\S]*?)\};)raw").arg(QRegularExpression::escape(objName));
                            auto objDefMatch = QRegularExpression(objPattern).match(baseJs);
                            if (objDefMatch.hasMatch()) {
                                QString script = QString(
                                    "var %1 = {%2};\n"
                                    "var decryptFunc = function(%3) {%4};\n"
                                    "decryptFunc('%5');"
                                ).arg(objName, objDefMatch.captured(1), paramName, funcBody, s);

                                QJSEngine engine;
                                QJSValue res = engine.evaluate(script);
                                if (!res.isError() && res.isString()) {
                                    return res.toString();
                                }
                            }
                        }
                    }
                }
            }
        }
        splitIdx = baseJs.indexOf(".split(", splitIdx + 1);
    }
    return "";
}

/**
 * @brief Трансформация n-токена для предотвращения троттлинга скорости YouTube (yt-dlp алгоритм).
 */
QString YouTubeExtractor::transformNToken(const QString& rawN, const QString& baseJs) {
    if (rawN.isEmpty() || baseJs.isEmpty()) return rawN;

    QString nFuncName;
    QList<QRegularExpression> nPatterns = {
        QRegularExpression(R"raw(\.get\("n"\)\)&&\([a-zA-Z0-9_$]+=\s*([a-zA-Z0-9_$]+(?:\[\d+\])?)\()raw"),
        QRegularExpression(R"raw([a-zA-Z0-9_$]+=\s*([a-zA-Z0-9_$]+)\([a-zA-Z0-9_$]+\.get\("n"\)\))raw"),
        QRegularExpression(R"raw([a-zA-Z0-9_$]+\.get\("n"\)\)&&\([a-zA-Z0-9_$]+=\s*([a-zA-Z0-9_$]+)\()raw"),
        QRegularExpression(R"raw(function\s+([a-zA-Z0-9_$]+)\s*\([a-zA-Z0-9_$]+\)\s*\{\s*var\s+[a-zA-Z0-9_$]+\s*=\s*[a-zA-Z0-9_$]+\.split\(""\))raw")
    };

    for (const auto& pat : nPatterns) {
        auto m = pat.match(baseJs);
        if (m.hasMatch()) {
            nFuncName = m.captured(1);
            break;
        }
    }

    if (nFuncName.isEmpty()) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Could not locate n-function name in base.js");
        return rawN;
    }

    QString escapedName = QRegularExpression::escape(nFuncName);
    QRegularExpression defRe(QString(R"raw((?:var\s+|let\s+|const\s+)?%1\s*=\s*function\s*\(([^)]*)\)\s*\{)raw").arg(escapedName));
    QRegularExpressionMatch defMatch = defRe.match(baseJs);
    if (!defMatch.hasMatch()) {
        defRe = QRegularExpression(QString(R"raw(function\s+%1\s*\(([^)]*)\)\s*\{)raw").arg(escapedName));
        defMatch = defRe.match(baseJs);
    }

    if (!defMatch.hasMatch()) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Could not find definition for n-function " + nFuncName.toStdString());
        return rawN;
    }

    int bodyStart = defMatch.capturedEnd(0) - 1;
    int open = 0, bodyEnd = -1;
    for (int i = bodyStart; i < baseJs.length(); ++i) {
        if (baseJs[i] == '{') open++;
        else if (baseJs[i] == '}') {
            open--;
            if (open == 0) {
                bodyEnd = i;
                break;
            }
        }
    }

    if (bodyEnd == -1) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: Failed to parse n-function brace balance.");
        return rawN;
    }

    QString funcBody = baseJs.mid(bodyStart + 1, bodyEnd - bodyStart - 1);
    QString paramName = defMatch.captured(1).trimmed();

    QString helperDefs;
    QRegularExpression helperRe(R"raw(([a-zA-Z0-9_$]+)\.[a-zA-Z0-9_$]+\s*\()raw");
    auto helperIter = helperRe.globalMatch(funcBody);
    QSet<QString> seenHelpers;
    while (helperIter.hasNext()) {
        QString helperObj = helperIter.next().captured(1);
        if (helperObj != paramName && !seenHelpers.contains(helperObj)) {
            seenHelpers.insert(helperObj);
            QRegularExpression objDefRe(QString(R"raw((?:var\s+|let\s+|const\s+)?%1\s*=\s*\{([\s\S]*?)\};)raw").arg(QRegularExpression::escape(helperObj)));
            auto objMatch = objDefRe.match(baseJs);
            if (objMatch.hasMatch()) {
                helperDefs += objMatch.captured(0) + "\n";
            }
        }
    }

    QString evalScript = QString(
        "%1\n"
        "var nTransformFunc = function(%2) { %3 };\n"
        "nTransformFunc('%4');"
    ).arg(helperDefs, paramName, funcBody, rawN);

    QJSEngine engine;
    QJSValue res = engine.evaluate(evalScript);

    if (res.isError()) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: n-token QJSEngine error: " + res.toString().toStdString());
        return rawN;
    }

    QString transformed = res.toString();
    if (transformed.isEmpty() || transformed == rawN) {
        Logger::Log(LogLevel::WARNING, "YouTubeExtractor: n-token transformed result is empty or identical.");
        return rawN;
    }

    return transformed;
}
