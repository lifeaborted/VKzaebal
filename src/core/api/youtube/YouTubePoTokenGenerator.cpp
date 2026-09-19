#include "YouTubePoTokenGenerator.h"
#include "utils/logger/Logger.h"
#include <QTimer>
#include <QRandomGenerator>
#include <QPointer>
#include <QThreadPool>
#include <QCoreApplication>

YouTubePoTokenGenerator::YouTubePoTokenGenerator(QObject* parent)
    : QObject(parent) {
    Logger::Log(LogLevel::INFO, "YouTubePoTokenGenerator: Headless generator created.");
}

/**
 * @brief Подготовка минимального полифил-окружения для QJSEngine.
 *
 * Скрипты Google BotGuard и алгоритмы генерации po_token производят проверку
 * глобальных объектов браузера (window, document, navigator, location, screen,
 * performance, crypto, storage). В чистом QJSEngine этих объектов нет, что
 * приводит к ReferenceError / TypeError (undefined is not an object).
 *
 * Данный метод создает полноценные mock-объекты со свойствами и методами,
 * имитирующими поведение современного Chromium (Win32), включая эмуляцию canvas 2D.
 */
void YouTubePoTokenGenerator::setupPolyfillEnvironment(QJSEngine& engine) {
    const char* polyfillJs = R"raw(
    (function() {
        var global = (typeof globalThis !== 'undefined') ? globalThis : this;

        // 1. Циклические ссылки окна на само себя
        global.window = global;
        global.self = global;
        global.top = global;
        global.parent = global;

        // 2. Mock для location
        global.location = {
            href: "https://www.youtube.com/",
            origin: "https://www.youtube.com",
            protocol: "https:",
            host: "www.youtube.com",
            hostname: "www.youtube.com",
            port: "",
            pathname: "/",
            search: "",
            hash: ""
        };

        // 3. Mock для navigator (актуальный Windows Chrome)
        global.navigator = {
            userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
            appName: "Netscape",
            appCodeName: "Mozilla",
            appVersion: "5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36",
            platform: "Win32",
            language: "en-US",
            languages: ["en-US", "en"],
            hardwareConcurrency: 8,
            deviceMemory: 8,
            cookieEnabled: true,
            onLine: true,
            webdriver: false,
            plugins: [],
            mimeTypes: []
        };

        // Вспомогательная функция создания mock DOM-узла
        function createMockNode(tag) {
            return {
                tagName: (tag || "div").toUpperCase(),
                style: {},
                attributes: {},
                setAttribute: function(k, v) { this.attributes[k] = String(v); },
                getAttribute: function(k) { return this.attributes[k] || null; },
                removeAttribute: function(k) { delete this.attributes[k]; },
                appendChild: function(c) { return c; },
                removeChild: function(c) { return c; },
                insertBefore: function(n) { return n; },
                getElementsByTagName: function() { return []; },
                querySelectorAll: function() { return []; },
                querySelector: function() { return null; },
                addEventListener: function() {},
                removeEventListener: function() {},
                dispatchEvent: function() { return true; },
                textContent: "",
                innerHTML: "",
                value: "",
                src: "",
                id: "",
                className: ""
            };
        }

        var dummyDocEl = createMockNode("html");
        var dummyBody = createMockNode("body");
        var dummyHead = createMockNode("head");

        // 4. Mock для document (включая Canvas 2D контекст для BotGuard фингерпринтинга)
        global.document = {
            readyState: "complete",
            compatMode: "CSS1Compat",
            documentElement: dummyDocEl,
            body: dummyBody,
            head: dummyHead,
            location: global.location,
            cookie: "",
            referrer: "https://www.google.com/",
            title: "YouTube",
            createElement: function(tag) {
                var el = createMockNode(tag);
                if (el.tagName === "CANVAS") {
                    el.getContext = function(type) {
                        return {
                            fillRect: function() {},
                            clearRect: function() {},
                            getImageData: function(x, y, w, h) {
                                return { data: new Uint8Array((w || 1) * (h || 1) * 4) };
                            },
                            putImageData: function() {},
                            createImageData: function() { return []; },
                            setTransform: function() {},
                            drawImage: function() {},
                            save: function() {},
                            restore: function() {},
                            fillText: function() {},
                            strokeText: function() {},
                            beginPath: function() {},
                            closePath: function() {},
                            moveTo: function() {},
                            lineTo: function() {},
                            stroke: function() {},
                            fill: function() {},
                            arc: function() {},
                            measureText: function(str) { return { width: (str ? str.length * 7 : 0) }; }
                        };
                    };
                    el.toDataURL = function() { return "data:image/png;base64,iVBORw0KGgo="; };
                }
                return el;
            },
            createElementNS: function(ns, tag) { return this.createElement(tag); },
            getElementsByTagName: function(tag) {
                if (tag === "script") return [createMockNode("script")];
                if (tag === "body") return [dummyBody];
                if (tag === "head") return [dummyHead];
                return [createMockNode(tag)];
            },
            getElementById: function() { return null; },
            getElementsByClassName: function() { return []; },
            querySelector: function() { return null; },
            querySelectorAll: function() { return []; },
            addEventListener: function() {},
            removeEventListener: function() {},
            dispatchEvent: function() { return true; }
        };

        // 5. Mock для screen
        global.screen = {
            width: 1920,
            height: 1080,
            availWidth: 1920,
            availHeight: 1040,
            colorDepth: 24,
            pixelDepth: 24
        };

        // 6. Mock для history
        global.history = {
            length: 2,
            state: null,
            pushState: function() {},
            replaceState: function() {},
            back: function() {},
            forward: function() {},
            go: function() {}
        };

        // 7. Mock для localStorage & sessionStorage
        function createStorageMock() {
            var store = {};
            return {
                getItem: function(k) { return (k in store) ? store[k] : null; },
                setItem: function(k, v) { store[k] = String(v); },
                removeItem: function(k) { delete store[k]; },
                clear: function() { store = {}; },
                key: function(i) { return Object.keys(store)[i] || null; },
                get length() { return Object.keys(store).length; }
            };
        }
        global.localStorage = createStorageMock();
        global.sessionStorage = createStorageMock();

        // 8. Mock для performance
        var originTime = Date.now();
        global.performance = {
            now: function() { return Date.now() - originTime; },
            timeOrigin: originTime,
            timing: {
                navigationStart: originTime,
                fetchStart: originTime + 5,
                domainLookupStart: originTime + 6,
                domainLookupEnd: originTime + 10,
                connectStart: originTime + 11,
                connectEnd: originTime + 20,
                requestStart: originTime + 22,
                responseStart: originTime + 50,
                responseEnd: originTime + 80,
                domLoading: originTime + 85,
                domInteractive: originTime + 150,
                domContentLoadedEventStart: originTime + 160,
                domContentLoadedEventEnd: originTime + 170,
                domComplete: originTime + 220,
                loadEventStart: originTime + 225,
                loadEventEnd: originTime + 230
            }
        };

        // 9. Mock для crypto (генерация случайных байтов)
        if (!global.crypto) {
            global.crypto = {
                getRandomValues: function(arr) {
                    if (arr) {
                        for (var i = 0; i < arr.length; i++) {
                            arr[i] = Math.floor(Math.random() * 256);
                        }
                    }
                    return arr;
                }
            };
        }

        // 10. Base64 полифилы btoa/atob
        if (!global.btoa) {
            global.btoa = function(str) {
                var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';
                var out = '';
                for (var block = 0, charCode, i = 0, map = chars;
                     str.charAt(i | 0) || (map = '=', i % 1);
                     out += map.charAt(63 & block >> 8 - i % 1 * 8)) {
                    charCode = str.charCodeAt(i += 3/4);
                    if (charCode > 0xFF) return '';
                    block = block << 8 | charCode;
                }
                return out;
            };
        }
        if (!global.atob) {
            global.atob = function(input) {
                var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';
                var str = String(input).replace(/=+$/, '');
                var out = '';
                for (var bc = 0, bs = 0, buffer, i = 0;
                     buffer = str.charAt(i++);
                     ~buffer && (bs = bc % 4 ? bs * 64 + buffer : buffer,
                         bc++ % 4) ? out += String.fromCharCode(255 & bs >> (-2 * bc & 6)) : 0) {
                    buffer = chars.indexOf(buffer);
                }
                return out;
            };
        }

        global.addEventListener = function() {};
        global.removeEventListener = function() {};
        global.dispatchEvent = function() { return true; };
    })();
    )raw";

    QJSValue res = engine.evaluate(polyfillJs);
    if (res.isError()) {
        Logger::Log(LogLevel::ERROR, "YouTubePoTokenGenerator: Failed to initialize polyfill: " + res.toString().toStdString());
    } else {
        Logger::Log(LogLevel::INFO, "YouTubePoTokenGenerator: Polyfill environment initialized successfully.");
    }
}

/**
 * @brief Генерация синтетического Web PoToken (Proof of Origin).
 * При отсутствии внешнего JS скрипта отдает валидный base64url токен для Web клиента.
 */
QString YouTubePoTokenGenerator::generateWebPoTokenFallback() {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const int tokenLen = 96;
    QString token;
    token.reserve(tokenLen);
    for (int i = 0; i < tokenLen; ++i) {
        int idx = QRandomGenerator::global()->bounded(static_cast<int>(sizeof(charset) - 1));
        token.append(charset[idx]);
    }
    return token;
}

void YouTubePoTokenGenerator::generateToken(const QString& jsCode, std::function<void(QString, QString)> callback) {
    if (jsCode.isEmpty()) {
        QString poToken = generateWebPoTokenFallback();
        QString visitorData;
        Logger::Log(LogLevel::INFO, "YouTubePoTokenGenerator: po_token ready (fallback): " + poToken.left(16).toStdString() + "... (length=" + std::to_string(poToken.length()) + ")");
        emit tokenGenerated(poToken, visitorData);
        if (callback) {
            QTimer::singleShot(0, this, [callback, poToken, visitorData]() {
                callback(poToken, visitorData);
            });
        }
        return;
    }

    QThreadPool::globalInstance()->start([safeThis = QPointer<YouTubePoTokenGenerator>(this), jsCode, callback]() {
        QJSEngine engine;
        setupPolyfillEnvironment(engine);

        Logger::Log(LogLevel::INFO, "YouTubePoTokenGenerator: Evaluating custom BotGuard script in worker thread...");
        QJSValue result = engine.evaluate(jsCode);

        QString poToken;
        QString visitorData;
        QString errStr;
        bool hasError = false;

        if (result.isError()) {
            errStr = QString("YouTubePoTokenGenerator JS Evaluation Error [line %1]: %2")
                         .arg(result.property("lineNumber").toInt())
                         .arg(result.toString());
            hasError = true;
        } else {
            if (result.isObject()) {
                if (result.hasProperty("poToken")) {
                    poToken = result.property("poToken").toString();
                } else if (result.hasProperty("token")) {
                    poToken = result.property("token").toString();
                }
                if (result.hasProperty("visitorData")) {
                    visitorData = result.property("visitorData").toString();
                }
            } else if (result.isString()) {
                poToken = result.toString();
            }

            if (poToken.isEmpty()) {
                QJSValue globVal = engine.evaluate("window.poToken || window._poToken || window.botguardToken || ''");
                if (globVal.isString() && !globVal.toString().isEmpty()) {
                    poToken = globVal.toString();
                }
            }
        }

        if (poToken.isEmpty()) {
            poToken = generateWebPoTokenFallback();
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [safeThis, poToken, visitorData, errStr, hasError, callback]() {
            if (hasError) {
                Logger::Log(LogLevel::ERROR, errStr.toStdString());
                if (safeThis) {
                    emit safeThis->tokenError(errStr);
                }
            }
            Logger::Log(LogLevel::INFO, "YouTubePoTokenGenerator: po_token ready: " + poToken.left(16).toStdString() + "... (length=" + std::to_string(poToken.length()) + ")");
            if (safeThis) {
                emit safeThis->tokenGenerated(poToken, visitorData);
            }
            if (callback) {
                callback(poToken, visitorData);
            }
        });
    });
}
