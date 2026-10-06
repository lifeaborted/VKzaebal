#include "TuiThemeConfig.h"
#include "utils/path/PathManager.h"
#include "utils/logger/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include <QCoreApplication>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace tui {

static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string StripComment(const std::string& line) {
    size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
    if (firstNonSpace != std::string::npos && (line[firstNonSpace] == '#' || line[firstNonSpace] == ';')) {
        return "";
    }

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == '#' || c == ';') {
            // Check if it's a hex color '#RRGGBB'
            if (c == '#' && i + 6 < line.length()) {
                bool isHex = true;
                for (int j = 1; j <= 6; ++j) {
                    if (!std::isxdigit(static_cast<unsigned char>(line[i + j]))) {
                        isHex = false;
                        break;
                    }
                }
                if (isHex) {
                    if (i + 7 >= line.length() || line[i + 7] == ' ' || line[i + 7] == '\t' ||
                        line[i + 7] == ';' || line[i + 7] == ',' || line[i + 7] == '\r' || line[i + 7] == '\n') {
                        continue; // Valid hex color, not a comment!
                    }
                }
            }

            // Check if preceded by whitespace
            if (i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
                if (c == '#') {
                    return line.substr(0, i);
                }
                if (c == ';') {
                    size_t nextNonSpace = line.find_first_not_of(" \t", i + 1);
                    if (nextNonSpace != std::string::npos && !std::isdigit(static_cast<unsigned char>(line[nextNonSpace])) && line[nextNonSpace] != '#') {
                        return line.substr(0, i);
                    }
                }
            }
        }
    }
    return line;
}

RGB TuiThemeConfig::ParseRgbStruct(const std::string& str, const RGB& fallback) {
    std::string s = Trim(str);
    if (s.empty()) return fallback;

    // Check for HEX format (#RRGGBB or RRGGBB)
    if (s[0] == '#' || (s.length() == 6 && std::isxdigit(static_cast<unsigned char>(s[0])) &&
                        std::isxdigit(static_cast<unsigned char>(s[1])) &&
                        std::isxdigit(static_cast<unsigned char>(s[2])) &&
                        std::isxdigit(static_cast<unsigned char>(s[3])) &&
                        std::isxdigit(static_cast<unsigned char>(s[4])) &&
                        std::isxdigit(static_cast<unsigned char>(s[5])))) {
        if (s[0] == '#') s = s.substr(1);
        if (s.length() == 6) {
            try {
                unsigned int val = std::stoul(s, nullptr, 16);
                int r = (val >> 16) & 0xFF;
                int g = (val >> 8) & 0xFF;
                int b = val & 0xFF;
                return {r, g, b};
            } catch (...) {}
        }
    }

    // Check for decimal RGB format ("255, 128, 0" or "255 128 0")
    std::vector<int> nums;
    std::string token;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            token += c;
        } else if (c == ',' || c == ' ' || c == '\t') {
            if (!token.empty()) {
                try { nums.push_back(std::clamp(std::stoi(token), 0, 255)); } catch (...) {}
                token.clear();
            }
        }
    }
    if (!token.empty()) {
        try { nums.push_back(std::clamp(std::stoi(token), 0, 255)); } catch (...) {}
    }

    if (nums.size() >= 3) {
        return {nums[0], nums[1], nums[2]};
    }

    return fallback;
}

ftxui::Color TuiThemeConfig::ParseColor(const std::string& str, const ftxui::Color& fallback) {
    RGB rgb = ParseRgbStruct(str, {-1, -1, -1});
    if (rgb.r < 0) return fallback;
    return ftxui::Color::RGB(rgb.r, rgb.g, rgb.b);
}

std::vector<RGB> TuiThemeConfig::ParseColorList(const std::string& str, const std::vector<RGB>& fallback) {
    std::vector<RGB> result;
    if (str.find(';') != std::string::npos) {
        std::stringstream ss(str);
        std::string part;
        while (std::getline(ss, part, ';')) {
            std::string trimmed = Trim(part);
            if (!trimmed.empty()) {
                RGB rgb = ParseRgbStruct(trimmed, {-1, -1, -1});
                if (rgb.r >= 0) {
                    result.push_back(rgb);
                }
            }
        }
    } else if (str.find(',') != std::string::npos && str.find('#') != std::string::npos) {
        std::stringstream ss(str);
        std::string part;
        while (std::getline(ss, part, ',')) {
            std::string trimmed = Trim(part);
            if (!trimmed.empty()) {
                RGB rgb = ParseRgbStruct(trimmed, {-1, -1, -1});
                if (rgb.r >= 0) {
                    result.push_back(rgb);
                }
            }
        }
    } else {
        RGB rgb = ParseRgbStruct(str, {-1, -1, -1});
        if (rgb.r >= 0) {
            result.push_back(rgb);
        }
    }

    if (result.empty()) return fallback;
    return result;
}

TuiThemeConfig::TuiThemeConfig(QObject* parent)
    : QObject(parent),
      m_theme(GetDefaultTheme()) {
    // Default background config
    m_bgConfig.mode = BackgroundMode::GRADIENT;
    m_bgConfig.color = ftxui::Color::RGB(13, 17, 23);
    m_bgConfig.gradientStops = {
        {35, 15, 45},
        {18, 12, 28},
        {10, 8, 18}
    };

    // Default visualizer config
    m_visConfig.mode = 0;
    m_visConfig.barWidth = 1;
    m_visConfig.barSpacing = 1;
    m_visConfig.smoothing = 0.72f;
    m_visConfig.sensitivity = 1.25f;
    m_visConfig.showPeaks = true;
    m_visConfig.colorPeak = ftxui::Color::RGB(255, 123, 114);
    m_visConfig.gradientStops = {
        {50, 255, 150},   // Bottom / low level (Green)
        {255, 184, 108},  // Mid level (Amber)
        {255, 80, 80}     // Top / peak level (Red)
    };

    // Default service colors
    m_serviceColors["VK"] = ftxui::Color::RGB(70, 128, 255);
    m_serviceColors["VKontakte"] = ftxui::Color::RGB(70, 128, 255);
    m_serviceColors["Yandex"] = ftxui::Color::RGB(255, 204, 0);
    m_serviceColors["Ya"] = ftxui::Color::RGB(255, 204, 0);
    m_serviceColors["SoundCloud"] = ftxui::Color::RGB(255, 102, 0);
    m_serviceColors["SC"] = ftxui::Color::RGB(255, 102, 0);
    m_serviceColors["YouTube"] = ftxui::Color::RGB(255, 51, 51);
    m_serviceColors["YT"] = ftxui::Color::RGB(255, 51, 51);
    m_serviceColors["Spotify"] = ftxui::Color::RGB(30, 215, 96);
    m_serviceColors["Offline"] = ftxui::Color::RGB(150, 150, 150);
    m_serviceColors["All"] = ftxui::Color::RGB(80, 255, 150);
}

void TuiThemeConfig::Initialize() {
    EnsureConfigFileExists();
    Reload();

    m_watcher = new QFileSystemWatcher(this);
    if (!m_configPath.isEmpty()) {
        m_watcher->addPath(m_configPath);
    }
    // Also watch root project directory config if different from current
    QString rootConfig = "C:/others/Codes/audio player/ftxui.cfg";
    if (QFile::exists(rootConfig) && rootConfig != m_configPath) {
        m_watcher->addPath(rootConfig);
    }
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &TuiThemeConfig::OnConfigFileChanged);
}

void TuiThemeConfig::EnsureConfigFileExists() {
    QString rootPath = "C:/others/Codes/audio player/ftxui.cfg";
    QString localPath = QDir::currentPath() + "/ftxui.cfg";
    QString appDirPath = QCoreApplication::applicationDirPath() + "/ftxui.cfg";
    QString appDataPath = PathManager::GetAppDataDir() + "/ftxui.cfg";

    if (QFile::exists(localPath)) {
        m_configPath = localPath;
    } else if (QFile::exists(rootPath)) {
        m_configPath = rootPath;
    } else if (QFile::exists(appDirPath)) {
        m_configPath = appDirPath;
    } else if (QFile::exists(appDataPath)) {
        m_configPath = appDataPath;
    } else {
        m_configPath = localPath;
        CreateDefaultConfigFile(m_configPath);
    }

    // Ensure root project path also exists so user can edit from project root easily
    if (!QFile::exists(rootPath) && m_configPath != rootPath) {
        CreateDefaultConfigFile(rootPath);
    }
}

void TuiThemeConfig::CreateDefaultConfigFile(const QString& path) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        out << "# ==============================================================================\n";
        out << "# VKAudioPlayer - Настройки темы FTXUI, фона и визуализатора\n";
        out << "# Все цвета задаются в формате RGB (R, G, B), например: 255, 128, 0\n";
        out << "# Также поддерживается HEX формат (#RRGGBB)\n";
        out << "# Изменения применяются на лету (Hot-Reload) сразу при сохранении файла!\n";
        out << "# ==============================================================================\n\n";

        out << "[Background]\n";
        out << "# Режим заднего фона: mono (моноцвет) или gradient (вертикальный градиент)\n";
        out << "mode = gradient\n\n";
        out << "# Однотонный цвет фона (используется при mode = mono)\n";
        out << "color = 13, 17, 23\n\n";
        out << "# Цвета для вертикального градиента фона (сверху вниз через точку с запятой)\n";
        out << "# Можно указать любое количество оттенков!\n";
        out << "gradient = 35, 15, 45; 18, 12, 28; 10, 8, 18\n\n";

        out << "[Theme]\n";
        out << "# Фон боковой панели (сайдбар со списком источников и плейлистов)\n";
        out << "panel_bg = 22, 27, 34\n\n";
        out << "# Фон карточек и информационных блоков\n";
        out << "card_bg = 27, 33, 44\n\n";
        out << "# Цвет разделительных линий и рамок\n";
        out << "border = 48, 54, 61\n\n";
        out << "# Цвет рамок в фокусе / активных элементов\n";
        out << "border_focus = 88, 166, 255\n\n";
        out << "# Основной текст (названия треков, заголовки)\n";
        out << "text = 201, 209, 217\n\n";
        out << "# Приглушенный текст (авторы песен, подсказки горячих клавиш, время)\n";
        out << "text_muted = 139, 148, 158\n\n";
        out << "# Главный акцентный цвет (кнопка Play, индикаторы)\n";
        out << "accent = 80, 255, 150\n\n";
        out << "# Дополнительные акцентные цвета\n";
        out << "accent_cyan = 88, 166, 255\n";
        out << "accent_orange = 255, 184, 108\n";
        out << "accent_purple = 188, 140, 255\n";
        out << "accent_red = 255, 123, 114\n\n";
        out << "# Подсветка строки списка при наведении/навигации курсора\n";
        out << "highlight = 33, 38, 45\n\n";
        out << "# Подсветка строки играющего в данный момент трека\n";
        out << "active_row = 31, 53, 41\n\n";
        out << "# Настройки цветов полосы прогресса трека (проигранная часть, ползунок, оставшаяся часть)\n";
        out << "progress_played = 80, 255, 150\n";
        out << "progress_thumb = 255, 255, 255\n";
        out << "progress_remaining = 70, 70, 70\n\n";

        out << "[Services]\n";
        out << "# Цвета плашек и бейджей источников музыки в формате RGB (R, G, B):\n";
        out << "vk = 70, 128, 255\n";
        out << "yandex = 255, 204, 0\n";
        out << "soundcloud = 255, 102, 0\n";
        out << "youtube = 255, 51, 51\n";
        out << "spotify = 30, 215, 96\n";
        out << "offline = 150, 150, 150\n\n";

        out << "[Visualizer]\n";
        out << "# Стиль визуализатора:\n";
        out << "# 0 = Студийный эквалайзер (CAVA-стиль, плавные столбики с пробелами)\n";
        out << "# 1 = Симметричная волна (расходится вверх и вниз от центра)\n";
        out << "# 2 = Шелковая волна (плавные контуры, режим Canvas)\n";
        out << "mode = 0\n\n";
        out << "# Ширина каждого столбика эквалайзера в знакоместах (1 или 2)\n";
        out << "bar_width = 1\n\n";
        out << "# Расстояние между столбиками эквалайзера (0 = вплотную, 1 = через пробел)\n";
        out << "bar_spacing = 1\n\n";
        out << "# Плавность падения столбиков (от 0.1 до 0.98; выше = медленнее и плавнее)\n";
        out << "smoothing = 0.72\n\n";
        out << "# Чувствительность / множитель высоты столбиков\n";
        out << "sensitivity = 1.25\n\n";
        out << "# Отображать парящие пиковые маркеры над столбиками (true / false)\n";
        out << "show_peaks = true\n\n";
        out << "# Цвет парящих пиковых шапок\n";
        out << "color_peak = 255, 123, 114\n\n";
        out << "# Вертикальный градиент громкости столбиков (снизу вверх: тихий -> средний -> пик):\n";
        out << "# Указывается через точку с запятой в формате RGB (R, G, B)\n";
        out << "gradient = 50, 255, 150; 255, 184, 108; 255, 80, 80\n";

        file.close();
        Logger::Log(LogLevel::INFO, "Created default FTXUI config at: " + path.toStdString());
    }
}

void TuiThemeConfig::Reload() {
    if (m_configPath.isEmpty() || !QFile::exists(m_configPath)) return;
    ParseConfigFile(m_configPath);
}

void TuiThemeConfig::ParseConfigFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    m_rawValues.clear();
    std::string currentSection = "";
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif

    while (!in.atEnd()) {
        std::string rawLine = in.readLine().toStdString();
        std::string line = Trim(StripComment(rawLine));
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.length() - 2);
            for (char& c : currentSection) c = std::tolower(c);
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eqPos));
        std::string val = Trim(line.substr(eqPos + 1));
        for (char& c : key) c = std::tolower(c);

        if (!currentSection.empty() && !key.empty()) {
            m_rawValues[currentSection][key] = val;
        }

        if (currentSection == "background") {
            if (key == "mode") {
                std::string lowerVal = val;
                for (char& c : lowerVal) c = std::tolower(c);
                if (lowerVal == "gradient") m_bgConfig.mode = BackgroundMode::GRADIENT;
                else m_bgConfig.mode = BackgroundMode::MONO;
            } else if (key == "color") {
                m_bgConfig.color = ParseColor(val, m_bgConfig.color);
            } else if (key == "gradient") {
                m_bgConfig.gradientStops = ParseColorList(val, m_bgConfig.gradientStops);
            }
        } else if (currentSection == "theme") {
            if (key == "bg") m_theme.bg = ParseColor(val, m_theme.bg);
            else if (key == "panel_bg") m_theme.panelBg = ParseColor(val, m_theme.panelBg);
            else if (key == "card_bg") m_theme.cardBg = ParseColor(val, m_theme.cardBg);
            else if (key == "border") m_theme.border = ParseColor(val, m_theme.border);
            else if (key == "border_focus") m_theme.borderFocus = ParseColor(val, m_theme.borderFocus);
            else if (key == "text") m_theme.text = ParseColor(val, m_theme.text);
            else if (key == "text_muted") m_theme.textMuted = ParseColor(val, m_theme.textMuted);
            else if (key == "accent") m_theme.accent = ParseColor(val, m_theme.accent);
            else if (key == "accent_cyan") m_theme.accentCyan = ParseColor(val, m_theme.accentCyan);
            else if (key == "accent_orange") m_theme.accentOrange = ParseColor(val, m_theme.accentOrange);
            else if (key == "accent_purple") m_theme.accentPurple = ParseColor(val, m_theme.accentPurple);
            else if (key == "accent_red") m_theme.accentRed = ParseColor(val, m_theme.accentRed);
            else if (key == "highlight") m_theme.highlight = ParseColor(val, m_theme.highlight);
            else if (key == "active_row") m_theme.activeRow = ParseColor(val, m_theme.activeRow);
            else if (key == "progress_played") m_theme.progressPlayed = ParseColor(val, m_theme.progressPlayed);
            else if (key == "progress_thumb") m_theme.progressThumb = ParseColor(val, m_theme.progressThumb);
            else if (key == "progress_remaining") m_theme.progressRemaining = ParseColor(val, m_theme.progressRemaining);
        } else if (currentSection == "services") {
            if (key == "vk") m_serviceColors["VK"] = m_serviceColors["VKontakte"] = ParseColor(val, m_serviceColors["VK"]);
            else if (key == "yandex") m_serviceColors["Yandex"] = m_serviceColors["Ya"] = m_serviceColors["YA"] = ParseColor(val, m_serviceColors["Yandex"]);
            else if (key == "soundcloud") m_serviceColors["SoundCloud"] = m_serviceColors["SC"] = ParseColor(val, m_serviceColors["SoundCloud"]);
            else if (key == "youtube") m_serviceColors["YouTube"] = m_serviceColors["YT"] = ParseColor(val, m_serviceColors["YouTube"]);
            else if (key == "spotify") m_serviceColors["Spotify"] = m_serviceColors["SP"] = ParseColor(val, m_serviceColors["Spotify"]);
            else if (key == "offline") m_serviceColors["Offline"] = m_serviceColors["OF"] = ParseColor(val, m_serviceColors["Offline"]);
            else if (key == "all") m_serviceColors["All"] = m_serviceColors["ALL"] = ParseColor(val, m_serviceColors["All"]);
        } else if (currentSection == "visualizer") {
            if (key == "mode") {
                try { m_visConfig.mode = std::clamp(std::stoi(val), 0, 2); } catch (...) {}
            } else if (key == "bar_width") {
                try { m_visConfig.barWidth = std::clamp(std::stoi(val), 1, 4); } catch (...) {}
            } else if (key == "bar_spacing") {
                try { m_visConfig.barSpacing = std::clamp(std::stoi(val), 0, 2); } catch (...) {}
            } else if (key == "smoothing") {
                try { m_visConfig.smoothing = std::clamp(std::stof(val), 0.1f, 0.98f); } catch (...) {}
            } else if (key == "sensitivity") {
                try { m_visConfig.sensitivity = std::clamp(std::stof(val), 0.2f, 5.0f); } catch (...) {}
            } else if (key == "show_peaks") {
                std::string lowerVal = val;
                for (char& c : lowerVal) c = std::tolower(c);
                m_visConfig.showPeaks = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
            } else if (key == "color_peak") {
                m_visConfig.colorPeak = ParseColor(val, m_visConfig.colorPeak);
            } else if (key == "gradient") {
                m_visConfig.gradientStops = ParseColorList(val, m_visConfig.gradientStops);
            }
        }
    }
}

ftxui::Color TuiThemeConfig::GetServiceColor(const std::string& source) const {
    auto it = m_serviceColors.find(source);
    if (it != m_serviceColors.end()) {
        return it->second;
    }
    std::string shortCode = FormatSourceShort(source);
    auto itShort = m_serviceColors.find(shortCode);
    if (itShort != m_serviceColors.end()) {
        return itShort->second;
    }
    return tui::GetServiceColor(source);
}

void TuiThemeConfig::OnConfigFileChanged(const QString& path) {
    // If root config changed and local config exists, keep them updated
    QString rootConfig = "C:/others/Codes/audio player/ftxui.cfg";
    QString targetPath = path;

    QTimer::singleShot(50, this, [this, targetPath, rootConfig]() {
        if (m_watcher) {
            if (!m_watcher->files().contains(m_configPath) && QFile::exists(m_configPath)) {
                m_watcher->addPath(m_configPath);
            }
            if (!m_watcher->files().contains(rootConfig) && QFile::exists(rootConfig)) {
                m_watcher->addPath(rootConfig);
            }
        }
        ParseConfigFile(targetPath);
        Logger::Log(LogLevel::INFO, "ftxui.cfg reloaded on the fly from: " + targetPath.toStdString());
        if (OnConfigChanged) {
            OnConfigChanged();
        }
    });
}

void TuiThemeConfig::SaveCfgValue(const QString& filePath, const std::string& section, const std::string& key, const std::string& value) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) return;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QStringList lines;
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    std::string targetSecLower = section;
    for (char& c : targetSecLower) c = std::tolower(c);
    std::string targetKeyLower = key;
    for (char& c : targetKeyLower) c = std::tolower(c);

    bool insideSection = false;
    bool keyUpdated = false;
    int sectionEndIdx = -1;

    for (int i = 0; i < lines.size(); ++i) {
        std::string lineStr = lines[i].toStdString();
        std::string stripped = Trim(lineStr);
        if (stripped.starts_with("[") && stripped.ends_with("]")) {
            std::string sec = stripped.substr(1, stripped.length() - 2);
            for (char& c : sec) c = std::tolower(c);
            if (sec == targetSecLower) {
                insideSection = true;
                sectionEndIdx = i + 1;
                continue;
            } else if (insideSection) {
                sectionEndIdx = i;
                break;
            }
        }

        if (insideSection) {
            sectionEndIdx = i + 1;
            size_t eqPos = lineStr.find('=');
            if (eqPos != std::string::npos) {
                std::string k = Trim(lineStr.substr(0, eqPos));
                for (char& c : k) c = std::tolower(c);
                if (k == targetKeyLower) {
                    lines[i] = QString::fromStdString(key + " = " + value);
                    keyUpdated = true;
                    break;
                }
            }
        }
    }

    if (!keyUpdated) {
        if (insideSection && sectionEndIdx >= 0) {
            lines.insert(sectionEndIdx, QString::fromStdString(key + " = " + value));
        } else {
            lines.append(QString::fromStdString("\n[" + section + "]"));
            lines.append(QString::fromStdString(key + " = " + value));
        }
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        for (const auto& line : lines) {
            out << line << "\n";
        }
        file.close();
    }
}

std::string TuiThemeConfig::GetRawValue(const std::string& section, const std::string& key, const std::string& fallback) const {
    std::string s = section;
    for (char& c : s) c = std::tolower(c);
    std::string k = key;
    for (char& c : k) c = std::tolower(c);

    auto secIt = m_rawValues.find(s);
    if (secIt != m_rawValues.end()) {
        auto keyIt = secIt->second.find(k);
        if (keyIt != secIt->second.end()) {
            return keyIt->second;
        }
    }
    return fallback;
}

void TuiThemeConfig::SetRawValue(const std::string& section, const std::string& key, const std::string& value) {
    std::string s = section;
    for (char& c : s) c = std::tolower(c);
    std::string k = key;
    for (char& c : k) c = std::tolower(c);

    m_rawValues[s][k] = value;

    QString rootConfig = "C:/others/Codes/audio player/ftxui.cfg";
    for (const QString& path : {m_configPath, rootConfig}) {
        if (!path.isEmpty() && QFile::exists(path)) {
            SaveCfgValue(path, section, key, value);
        }
    }
    Reload();
    if (OnConfigChanged) {
        OnConfigChanged();
    }
}

} // namespace tui
