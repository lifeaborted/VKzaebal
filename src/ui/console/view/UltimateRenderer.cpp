#include "UltimateRenderer.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "utils/path/PathManager.h"
#include "utils/logger/Logger.h"

#include <QSettings>
#include <QCoreApplication>
#include <QMetaObject>
#include <iostream>
#include <cmath>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

static int utf8_length(const std::string& str) {
    int len = 0;
    for (size_t i = 0; i < str.length(); ++i) {
        if ((str[i] & 0xC0) != 0x80) len++;
    }
    return len;
}

static std::string safeTruncate(const std::string& str, int maxChars) {
    std::string result;
    int chars = 0;
    for (size_t i = 0; i < str.length(); ) {
        if (chars >= maxChars) break;
        size_t charLen = 1;
        if ((str[i] & 0xE0) == 0xC0) charLen = 2;
        else if ((str[i] & 0xF0) == 0xE0) charLen = 3;
        else if ((str[i] & 0xF8) == 0xF0) charLen = 4;

        result.append(str, i, charLen);
        i += charLen;
        chars++;
    }
    return result;
}

// Парсер списка HEX цветов (напр. "#FF0000,#00FF00")
static std::vector<RGB> ParseColorsList(const std::string& str) {
    std::vector<RGB> res;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t\"\'"));
        item.erase(item.find_last_not_of(" \t\"\'") + 1);
        if (item.length() == 7 && item[0] == '#') {
            try {
                int r = std::stoi(item.substr(1, 2), nullptr, 16);
                int g = std::stoi(item.substr(3, 2), nullptr, 16);
                int b = std::stoi(item.substr(5, 2), nullptr, 16);
                res.push_back({r, g, b});
            } catch(...) {}
        }
    }
    return res;
}

// конвертер HEX в ANSI
static std::string HexToAnsiStr(const std::string& hex, bool isBg) {
    if (hex.length() == 7 && hex[0] == '#') {
        try {
            int r = std::stoi(hex.substr(1, 2), nullptr, 16);
            int g = std::stoi(hex.substr(3, 2), nullptr, 16);
            int b = std::stoi(hex.substr(5, 2), nullptr, 16);
            return "\033[" + std::to_string(isBg ? 48 : 38) + ";2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
        } catch(...) {}
    }
    std::string s = hex;
    size_t pos = 0;
    while ((pos = s.find("\\033", pos)) != std::string::npos) {
        s.replace(pos, 4, "\033");
        pos += 1;
    }
    return s;
}

// Математическая интерполяция градиента (Lerp) по массиву точек
static std::string GetInterpolatedColor(float t, const std::vector<RGB>& stops, bool isBg) {
    if (stops.empty()) return "";
    if (stops.size() == 1 || t <= 0.0f) {
        return "\033[" + std::to_string(isBg ? 48 : 38) + ";2;" + std::to_string(stops[0].r) + ";" + std::to_string(stops[0].g) + ";" + std::to_string(stops[0].b) + "m";
    }
    if (t >= 1.0f) {
        auto& last = stops.back();
        return "\033[" + std::to_string(isBg ? 48 : 38) + ";2;" + std::to_string(last.r) + ";" + std::to_string(last.g) + ";" + std::to_string(last.b) + "m";
    }

    float scaled_t = t * (stops.size() - 1);
    int idx = static_cast<int>(scaled_t);
    float fract = scaled_t - idx;

    const auto& c1 = stops[idx];
    const auto& c2 = stops[idx + 1];

    int r = c1.r + (c2.r - c1.r) * fract;
    int g = c1.g + (c2.g - c1.g) * fract;
    int b = c1.b + (c2.b - c1.b) * fract;

    return "\033[" + std::to_string(isBg ? 48 : 38) + ";2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
}

static void GetConsoleSize(int& width, int& height) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return;
    }
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
        height = w.ws_row;
        return;
    }
#endif
    width = 80; height = 24;
}

// --- ОСНОВНОЙ КЛАСС ---

UltimateRenderer::UltimateRenderer(IAudioEngine& audio, PlaylistManager& playlist)
    : m_audio(audio), m_playlist(playlist) {
    ReloadConfig();
}

void UltimateRenderer::SetStatusMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_statusMessage = msg;
    m_needsFullRedraw = true;
}

void UltimateRenderer::SetOverlay(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_overlayText = msg;
    m_needsFullRedraw = true;
}

void UltimateRenderer::ReloadConfig() {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    QSettings settings(PathManager::GetUltimateConfigPath(), QSettings::IniFormat);

    m_height = settings.value("Visualizer/Height", 8).toInt();
    m_width = settings.value("Visualizer/Width", 0).toInt();
    m_barWidth = settings.value("Visualizer/BarWidth", 1).toInt();
    m_barSpacing = settings.value("Visualizer/BarSpacing", 0).toInt();

    m_fps = settings.value("Visualizer/Framerate", 30).toInt();
    if (m_fps < 1) m_fps = 30;

    m_uiFps = settings.value("Interface/Framerate", 5).toInt();
    if (m_uiFps < 1) m_uiFps = 1;

    m_smoothing = settings.value("Visualizer/Smoothing", 0.5f).toFloat();
    if (m_smoothing < 0.0f) m_smoothing = 0.0f;
    if (m_smoothing > 0.99f) m_smoothing = 0.99f;

    int padLeft = settings.value("Visualizer/PaddingLeft", 2).toInt();
    m_paddingLeft = std::string(padLeft, ' ');

    // Чтение цветов визуала
    std::string fgColor = settings.value("Visualizer/Color", "gradient").toString().toStdString();
    if (!fgColor.empty() && fgColor[0] == '#') m_colorCode = HexToAnsiStr(fgColor, false);
    else m_colorCode = HexToAnsiStr(fgColor, false);
    m_gradientColors = ParseColorsList(settings.value("Visualizer/GradientColors", "#32FF96,#F0B432,#FF5050").toString().toStdString());

    // Чтение фона
    m_bgEnabled = settings.value("Background/Enabled", false).toBool();
    std::string bgColor = settings.value("Background/Color", "gradient").toString().toStdString();
    if (!bgColor.empty() && bgColor[0] == '#') m_bgColorCode = HexToAnsiStr(bgColor, true);
    else m_bgColorCode = HexToAnsiStr(bgColor, true);
    m_bgGradientColors = ParseColorsList(settings.value("Background/GradientColors", "#1E1E1E,#000000").toString().toStdString());

    m_needsFullRedraw = true;
}

void UltimateRenderer::Render() {
    std::vector<float> fft;
    if (m_audio.IsPlaying()) {
        fft = m_audio.GetSpectrumData();
    }
    if (fft.empty()) {
        fft.assign(256, 0.0f);
    }

    std::lock_guard<std::mutex> lock(m_renderMutex);

    int consoleWidth, consoleHeight;
    GetConsoleSize(consoleWidth, consoleHeight);
    static int s_lastConsoleWidth = 0;
    static int s_lastConsoleHeight = 0;

    if (consoleWidth != s_lastConsoleWidth || consoleHeight != s_lastConsoleHeight) {
        if (s_lastConsoleWidth != 0) m_needsFullRedraw = true;
        s_lastConsoleWidth = consoleWidth;
        s_lastConsoleHeight = consoleHeight;
    }

    // --- ТАЙМЕР РАЗДЕЛЬНОГО ФРЕЙМРЕЙТА ---
    auto now = std::chrono::steady_clock::now();
    bool updateUi = false;
    if (m_needsFullRedraw || std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastUiUpdate).count() >= (1000 / m_uiFps)) {
        updateUi = true;
        m_lastUiUpdate = now;
    }

    int fixedLines = 19;
    int availableForDynamic = consoleHeight - fixedLines;

    int currentHeight = m_height > 1 ? m_height - 1 : 1;
    int playlistLines = 5;

    if (availableForDynamic < 2) {
        currentHeight = 1;
        playlistLines = 1;
    } else {
        if (currentHeight > availableForDynamic - 3) {
            currentHeight = std::max(1, availableForDynamic - 3);
        }
        playlistLines = std::max(1, availableForDynamic - currentHeight);
    }

    int maxInnerChars = consoleWidth - utf8_length(m_paddingLeft) - 4;
    if (maxInnerChars < 20) maxInnerChars = 20;

    int maxBars = maxInnerChars / (m_barWidth + m_barSpacing);
    if (maxBars <= 0) maxBars = 1;

    int currentWidth = m_width;
    if (currentWidth <= 0 || currentWidth > maxBars) currentWidth = maxBars;

    int innerWidth = currentWidth * (m_barWidth + m_barSpacing) - m_barSpacing;
    if (innerWidth < 20) innerWidth = 20;

    std::vector<int> columns(currentWidth, 0);
    if (m_previousPeaks.size() != currentWidth) m_previousPeaks.assign(currentWidth, 0.0f);

    for (int x = 0; x < currentWidth; ++x) {
        float peak = 0.0f;
        int startBin = static_cast<int>(std::pow(2.0, x * 7.0 / currentWidth));
        int endBin = static_cast<int>(std::pow(2.0, (x + 1) * 7.0 / currentWidth));

        if (endBin <= startBin) endBin = startBin + 1;
        if (endBin > static_cast<int>(fft.size())) endBin = static_cast<int>(fft.size());

        for (int b = startBin; b < endBin; ++b) {
            if (fft[b] > peak) peak = fft[b];
        }

        if (peak < m_previousPeaks[x]) {
            m_previousPeaks[x] = m_previousPeaks[x] * m_smoothing + peak * (1.0f - m_smoothing);
        } else {
            m_previousPeaks[x] = peak * 0.8f + m_previousPeaks[x] * 0.2f;
        }

        int totalTicks = static_cast<int>(std::sqrt(m_previousPeaks[x]) * currentHeight * 8.0f);
        if (totalTicks < 0) totalTicks = 0;
        if (totalTicks > currentHeight * 8) totalTicks = currentHeight * 8;

        columns[x] = totalTicks;
    }

    // --- ОБНОВЛЕНИЕ КЭША ИНТЕРФЕЙСА ---
    if (updateUi) {
        Track currentTrack = m_playlist.GetCurrentTrack();

        std::string trackTitle = currentTrack.artist + " - " + currentTrack.title;
        if (trackTitle == " - ") trackTitle = "No Track Loaded";
        m_cTrackTitle = safeTruncate(trackTitle, consoleWidth - 20);

        double currentSec = m_audio.GetPositionSeconds();
        double totalSec = m_audio.GetLengthSeconds();
        if (totalSec <= 0.0) totalSec = static_cast<double>(currentTrack.duration);
        if (currentSec < 0.0) currentSec = 0.0;

        int curMin = static_cast<int>(currentSec) / 60, curSecInt = static_cast<int>(currentSec) % 60;
        int totMin = static_cast<int>(totalSec) / 60, totSecInt = static_cast<int>(totalSec) % 60;

        char timeBuf[128];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d / %02d:%02d", curMin, curSecInt, totMin, totSecInt);

        // 1. Статус проигрывания (теперь выводится один, прижат к левому краю)
        m_cTimeStatus = m_audio.IsPlaying() ? "\033[38;2;80;255;150m▶ Playing\033[0m" : "\033[38;2;150;150;150m■ Paused\033[0m";

        // 2. Рассчитываем ширину полосы с учетом таймера (чтобы не вылезать за спектрограмму)
        std::string timeStr = timeBuf;
        int timeLen = timeStr.length() + 2;
        int barInnerLength = innerWidth - timeLen;
        if (barInnerLength < 10) barInnerLength = 10;

        std::string barStr = "";

        if (totalSec > 0.0) {
            int filled = static_cast<int>((currentSec / totalSec) * barInnerLength);

            int thumbPos = filled;
            if (thumbPos >= barInnerLength) thumbPos = barInnerLength - 1;
            if (thumbPos < 0) thumbPos = 0;

            for (int i = 0; i < barInnerLength; ++i) {
                if (i == thumbPos) {
                    barStr += "\033[38;2;255;255;255m●\033[0m"; // Ползунок
                } else if (i < thumbPos) {
                    barStr += "\033[38;2;80;255;150m\xE2\x94\x81\033[0m"; // Заполненная часть
                } else {
                    barStr += "\033[38;2;80;80;80m\xE2\x94\x81\033[0m";   // Пустая часть
                }
            }
        } else {
            barStr += "\033[1;38;2;255;255;255m\xE2\x96\xA0\033[0m";
            for (int i = 1; i < barInnerLength; ++i) barStr += "\033[38;2;80;80;80m\xE2\x94\x81\033[0m";
        }

        m_cProgressBar = barStr + "  \033[38;2;150;150;150m" + timeStr + "\033[0m";

        int volPercent = static_cast<int>(std::round(m_audio.GetVolume() * 100));
        int volBars = (volPercent * 25) / 100;
        std::string volStr = "\033[38;2;150;150;150mEQ [\033[38;2;255;180;80m Flat \033[38;2;150;150;150m]     VOL \033[38;2;80;255;150m";

        for(int i = 0; i < 25; i++) {
            if (i < volBars) volStr += "█";
            else volStr += "\033[38;2;80;80;80m▒\033[38;2;80;255;150m";
        }
        volStr += "\033[0m \033[38;2;150;150;150m" + std::to_string(volPercent) + "%\033[0m";
        m_cVolumeEq = volStr;

        m_cPlaylistLines.clear();
        static std::vector<Track> cachedQueue;
        static std::string lastTrackId = "";

        if (cachedQueue.empty() || currentTrack.id != lastTrackId) {
            cachedQueue = m_playlist.GetQueueTracks();
            lastTrackId = currentTrack.id;
        }

        int trackIndex = 0;
        for (size_t i = 0; i < cachedQueue.size(); ++i) {
            if (cachedQueue[i].id == currentTrack.id) { trackIndex = i; break; }
        }

        std::string shufStr = m_playlist.IsShuffle() ? "\033[38;2;255;180;80m[Shuffle]\033[0m" : "\033[38;2;150;150;150m[Ordered]\033[0m";
        int repMode = m_playlist.GetRepeatMode();
        std::string repStr = (repMode == 1) ? "[Repeat: All]" : (repMode == 2 ? "[Repeat: One]" : "[Repeat: Off]");

        m_cPlaylistLines.push_back("\033[38;2;255;180;80m▶ Playlist\033[0m \033[38;2;100;100;100m—\033[0m " + shufStr + " \033[38;2;150;150;150m" + repStr + " [" + std::to_string(trackIndex + 1) + "/" + std::to_string(cachedQueue.size()) + "]\033[0m");

        m_cPlaylistLines.push_back("");

        int half = (playlistLines - 1) / 2;
        int startIdx = std::max(0, trackIndex - half);
        int endIdx = std::min(static_cast<int>(cachedQueue.size()) - 1, startIdx + playlistLines - 1);
        if (endIdx - startIdx < playlistLines - 1) {
            startIdx = std::max(0, endIdx - (playlistLines - 1));
        }

        for (int i = startIdx; i <= endIdx; ++i) {
            std::string tName = safeTruncate(cachedQueue[i].artist + " - " + cachedQueue[i].title, consoleWidth - 15);
            if (i == trackIndex) {
                m_cPlaylistLines.push_back("\033[38;2;80;255;150m▶ " + std::to_string(i + 1) + ". " + tName + "\033[0m");
            } else {
                m_cPlaylistLines.push_back("  \033[38;2;150;150;150m" + std::to_string(i + 1) + ". " + tName + "\033[0m");
            }
        }

        if (!m_statusMessage.empty() && m_overlayText.empty()) {
            std::string cleanMsg = m_statusMessage;
            cleanMsg.erase(std::remove(cleanMsg.begin(), cleanMsg.end(), '\n'), cleanMsg.end());
            cleanMsg.erase(std::remove(cleanMsg.begin(), cleanMsg.end(), '\r'), cleanMsg.end());
            m_cStatusMsg = "\033[38;2;255;180;80mℹ " + cleanMsg + "\033[0m";
        } else {
            m_cStatusMsg = "";
        }
    }

    // --- ФОРМИРОВАНИЕ КАДРА ---
    std::string frame;
    frame.reserve(consoleWidth * consoleHeight * 2);
    int drawnLines = 0;

    auto addLine = [&](std::string content) {
        if (drawnLines >= consoleHeight - 2) return;

        std::string bg = "";
        if (m_bgEnabled) {
            if (m_bgColorCode == "gradient") {
                bg = GetInterpolatedColor((float)drawnLines / std::max(1, consoleHeight - 1), m_bgGradientColors, true);
            } else {
                bg = m_bgColorCode;
            }

            std::string resetStr = "\033[0m" + bg;
            size_t pos = 0;
            while ((pos = content.find("\033[0m", pos)) != std::string::npos) {
                content.replace(pos, 4, resetStr);
                pos += resetStr.length();
            }
        }

        frame += bg + m_paddingLeft + content + "\033[0m" + bg + "\033[K\n";
        drawnLines++;
    };

    addLine("");

    if (!m_overlayText.empty()) {
        addLine("\033[38;2;80;255;150mC L I A M P   O V E R L A Y\033[0m");
        addLine("");

        std::stringstream ss(m_overlayText);
        std::string line;
        while(std::getline(ss, line)) {
            addLine(line);
        }
    } else {
        addLine("\033[38;2;80;255;150m \033[0m");
        addLine("\033[38;2;250;250;250m\033[1m" + m_cTrackTitle + "\033[0m");

        addLine("");
        addLine(m_cTimeStatus);
        addLine("");

        const char* blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        bool useGradient = (m_colorCode == "gradient");

        for (int y = currentHeight - 1; y >= 0; --y) {
            std::string lineContent = "";

            if (useGradient) {
                float t = static_cast<float>(y) / static_cast<float>(currentHeight > 1 ? currentHeight - 1 : 1);
                lineContent += GetInterpolatedColor(t, m_gradientColors, false);
            } else {
                lineContent += m_colorCode;
            }

            for (int x = 0; x < currentWidth; ++x) {
                int ticks = columns[x] - (y * 8);
                std::string barChar = " ";

                if (ticks >= 8) barChar = blocks[8];
                else if (ticks > 0) barChar = blocks[ticks];

                for(int bw = 0; bw < m_barWidth; ++bw) lineContent += barChar;
                if (x < currentWidth - 1) {
                    for(int bs = 0; bs < m_barSpacing; ++bs) lineContent += " ";
                }
            }
            addLine(lineContent);
        }

        addLine("");

        addLine(m_cProgressBar);
        addLine("");
        addLine(m_cVolumeEq);
        addLine("");
        addLine("");

        for (const auto& line : m_cPlaylistLines) {
            addLine(line);
        }
    }


    // 1. Выводим статус сразу под плейлистом
    if (!m_cStatusMsg.empty() && m_overlayText.empty()) {
        addLine(m_cStatusMsg);
    }

    // 2. Отрисовываем разделительную линию (плейлист теперь будет упираться прямо в нее)
    addLine("\033[38;2;80;80;80m" + std::string(innerWidth, '-') + "\033[0m");

    // 3. Футер с подсказками
    std::string footer = "\033[1;37m[P]\033[0m Play   \033[1;37m[N]\033[0m Next   \033[1;37m[B]\033[0m Prev   \033[1;37m[SH]\033[0m Shuffle   \033[1;37m[V 50]\033[0m Vol   \033[1;37m[H]\033[0m Help   \033[1;37m[Q]\033[0m Quit";

    std::string bg = "";
    if (m_bgEnabled) {
        bg = m_bgColorCode == "gradient" ? GetInterpolatedColor((float)drawnLines / std::max(1, consoleHeight - 1), m_bgGradientColors, true) : m_bgColorCode;
        std::string resetStr = "\033[0m" + bg;
        size_t pos = 0;
        while ((pos = footer.find("\033[0m", pos)) != std::string::npos) {
            footer.replace(pos, 4, resetStr);
            pos += resetStr.length();
        }
    }
    addLine(footer);

    while (drawnLines < consoleHeight - 2) {
        addLine("");
    }


    // --- ВЫВОД В КОНСОЛЬ ---
    if (m_needsFullRedraw || frame != m_lastPrintedStr) {
        m_lastPrintedStr = frame;
        bool forceFullRedraw = m_needsFullRedraw;
        m_needsFullRedraw = false;

        std::lock_guard<std::mutex> qLock(Logger::GetMutex());

        std::string out = "\033[?2026h\033[?25l";

        std::string promptBg = "";
        std::string promptFg = "\033[38;2;255;255;255m";

        if (m_bgEnabled) {
            promptBg = m_bgColorCode == "gradient" ? GetInterpolatedColor(1.0f, m_bgGradientColors, true) : m_bgColorCode;
        }

        if (forceFullRedraw) {
            out += "\033[2J";
            out += "\033[" + std::to_string(consoleHeight - 1) + ";1H" + promptBg + promptFg + "> \033[K";
            out += "\033[" + std::to_string(consoleHeight) + ";1H" + promptBg + "\033[K";
            out += "\033[" + std::to_string(consoleHeight - 1) + ";3H";
        }

        out += "\033[s";
        out += "\033[H";
        out += frame;

        out += "\033[u" + promptBg + promptFg + "\033[?25h\033[?2026l";

        fwrite(out.data(), 1, out.size(), stdout);
        fflush(stdout);
    }
}