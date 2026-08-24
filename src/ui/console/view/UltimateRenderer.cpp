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

static std::string GetGradientColor(float t) {
    int r, g, b;
    if (t < 0.5f) {
        float t2 = t * 2.0f;
        r = static_cast<int>(50 * (1 - t2) + 240 * t2);
        g = static_cast<int>(255 * (1 - t2) + 180 * t2);
        b = static_cast<int>(150 * (1 - t2) + 50 * t2);
    } else {
        float t2 = (t - 0.5f) * 2.0f;
        r = static_cast<int>(240 * (1 - t2) + 255 * t2);
        g = static_cast<int>(180 * (1 - t2) + 80 * t2);
        b = static_cast<int>(50 * (1 - t2) + 80 * t2);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm", r, g, b);
    return std::string(buf);
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
    
    m_smoothing = settings.value("Visualizer/Smoothing", 0.5f).toFloat();
    if (m_smoothing < 0.0f) m_smoothing = 0.0f;
    if (m_smoothing > 0.99f) m_smoothing = 0.99f;
    
    int padLeft = settings.value("Visualizer/PaddingLeft", 2).toInt();
    m_paddingLeft = std::string(padLeft, ' ');

    QString colorStr = settings.value("Visualizer/Color", "gradient").toString();
    colorStr.replace("\\\\033", "\033");
    colorStr.replace("\\033", "\033");
    colorStr.replace("\\\\x1b", "\x1b", Qt::CaseInsensitive);
    colorStr.replace("\\x1b", "\x1b", Qt::CaseInsensitive);
    m_colorCode = colorStr.toStdString();

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

    if (consoleWidth != s_lastConsoleWidth) {
        if (s_lastConsoleWidth != 0) m_needsFullRedraw = true;
        s_lastConsoleWidth = consoleWidth;
    }

    int fixedLines = 14;
    int availableForDynamic = consoleHeight - fixedLines - 2;

    int currentHeight = m_height;
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

        // --- ФИЗИКА ПАДЕНИЯ ---
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

    std::string frame = "";
    int linesCount = 0;

    // ==========================================
    // ЕСЛИ АКТИВЕН ОВЕРЛЕЙ (Меню Help/Search)
    // ==========================================
    if (!m_overlayText.empty()) {
        frame += m_paddingLeft + "\033[38;2;80;255;150mC L I A M P   O V E R L A Y\033[0m\033[K\n";
        linesCount++;
        frame += "\033[K\n";
        linesCount++;

        std::stringstream ss(m_overlayText);
        std::string line;
        while(std::getline(ss, line)) {
            frame += m_paddingLeft + line + "\033[K\n";
            linesCount++;
        }
    }
    // ==========================================
    // ИНАЧЕ РИСУЕМ СТАНДАРТНЫЙ ИНТЕРФЕЙС
    // ==========================================
    else {
        frame += m_paddingLeft + "\033[38;2;80;255;150mV K   A U D I O   P L A Y E R\033[0m\033[K\n";
        linesCount++;

        Track currentTrack = m_playlist.GetCurrentTrack();
        std::string trackTitle = currentTrack.artist + " - " + currentTrack.title;
        if (trackTitle == " - ") trackTitle = "No Track Loaded";
        trackTitle = safeTruncate(trackTitle, consoleWidth - 20);
        frame += m_paddingLeft + "\033[38;2;250;250;250m🎵 \033[1m" + trackTitle + "\033[0m\033[K\n";
        linesCount++;

        frame += m_paddingLeft + "\033[K\n";
        linesCount++;

        double currentSec = m_audio.GetPositionSeconds();
        double totalSec = m_audio.GetLengthSeconds();
        if (totalSec <= 0.0) totalSec = static_cast<double>(currentTrack.duration);
        if (currentSec < 0.0) currentSec = 0.0;

        int curMin = static_cast<int>(currentSec) / 60, curSecInt = static_cast<int>(currentSec) % 60;
        int totMin = static_cast<int>(totalSec) / 60, totSecInt = static_cast<int>(totalSec) % 60;

        char timeBuf[128];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d / %02d:%02d", curMin, curSecInt, totMin, totSecInt);

        std::string statusStr = m_audio.IsPlaying() ? "\033[38;2;80;255;150m▶ Playing\033[0m" : "\033[38;2;150;150;150m■ Paused\033[0m";
        frame += m_paddingLeft + "\033[38;2;150;150;150m" + timeBuf + "\033[0m        " + statusStr + "\033[K\n";
        linesCount++;

        frame += m_paddingLeft + "\033[K\n";
        linesCount++;

        const char* blocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        bool useGradient = (m_colorCode == "gradient");

        for (int y = currentHeight - 1; y >= 0; --y) {
            frame += m_paddingLeft;

            if (useGradient) {
                float t = static_cast<float>(y) / static_cast<float>(currentHeight > 1 ? currentHeight - 1 : 1);
                frame += GetGradientColor(t);
            } else {
                frame += m_colorCode;
            }

            for (int x = 0; x < currentWidth; ++x) {
                int ticks = columns[x] - (y * 8);
                std::string barChar = " ";

                if (ticks >= 8) barChar = blocks[8];
                else if (ticks > 0) barChar = blocks[ticks];

                for(int bw = 0; bw < m_barWidth; ++bw) frame += barChar;
                if (x < currentWidth - 1) {
                    for(int bs = 0; bs < m_barSpacing; ++bs) frame += " ";
                }
            }
            frame += "\033[0m\033[K\n";
            linesCount++;
        }

        int barInnerLength = innerWidth;
        std::string barStr = "";

        if (totalSec > 0.0) {
            int filled = static_cast<int>((currentSec / totalSec) * barInnerLength);
            if (filled > barInnerLength) filled = barInnerLength;

            for (int i = 0; i < barInnerLength; ++i) {
                if (i < filled) barStr += "\033[38;2;80;255;150m━\033[0m";
                else barStr += "\033[38;2;80;80;80m━\033[0m";
            }
        } else {
            for (int i = 0; i < barInnerLength; ++i) barStr += "\033[38;2;80;80;80m━\033[0m";
        }
        frame += m_paddingLeft + barStr + "\033[K\n";
        linesCount++;

        frame += m_paddingLeft + "\033[K\n";
        linesCount++;

        int volPercent = static_cast<int>(std::round(m_audio.GetVolume() * 100));
        int volBars = (volPercent * 25) / 100;
        std::string volStr = "\033[38;2;150;150;150mEQ [\033[38;2;255;180;80m Flat \033[38;2;150;150;150m]     VOL \033[38;2;80;255;150m";

        for(int i = 0; i < 25; i++) {
            if (i < volBars) volStr += "█";
            else volStr += "\033[38;2;80;80;80m▒\033[38;2;80;255;150m";
        }
        volStr += "\033[0m \033[38;2;150;150;150m" + std::to_string(volPercent) + "%\033[K\n";
        frame += m_paddingLeft + volStr;
        linesCount++;

        frame += m_paddingLeft + "\033[K\n";
        linesCount++;

        std::vector<Track> queue = m_playlist.GetQueueTracks();
        int trackIndex = 0;
        for (size_t i = 0; i < queue.size(); ++i) {
            if (queue[i].id == currentTrack.id) { trackIndex = i; break; }
        }

        std::string shufStr = m_playlist.IsShuffle() ? "\033[38;2;255;180;80m[Shuffle]\033[0m" : "\033[38;2;150;150;150m[Ordered]\033[0m";
        int repMode = m_playlist.GetRepeatMode();
        std::string repStr = (repMode == 1) ? "[Repeat: All]" : (repMode == 2 ? "[Repeat: One]" : "[Repeat: Off]");

        frame += m_paddingLeft + "\033[38;2;255;180;80m▶ Playlist\033[0m \033[38;2;100;100;100m—\033[0m " + shufStr + " \033[38;2;150;150;150m" + repStr + " [" + std::to_string(trackIndex + 1) + "/" + std::to_string(queue.size()) + "]\033[0m\033[K\n";
        linesCount++;

        int half = (playlistLines - 1) / 2;
        int startIdx = std::max(0, trackIndex - half);
        int endIdx = std::min(static_cast<int>(queue.size()) - 1, startIdx + playlistLines - 1);
        if (endIdx - startIdx < playlistLines - 1) {
            startIdx = std::max(0, endIdx - (playlistLines - 1));
        }

        for (int i = startIdx; i <= endIdx; ++i) {
            std::string tName = safeTruncate(queue[i].artist + " - " + queue[i].title, consoleWidth - 15);
            if (i == trackIndex) {
                frame += m_paddingLeft + "\033[38;2;80;255;150m▶ " + std::to_string(i + 1) + ". " + tName + "\033[0m\033[K\n";
            } else {
                frame += m_paddingLeft + "  \033[38;2;150;150;150m" + std::to_string(i + 1) + ". " + tName + "\033[0m\033[K\n";
            }
            linesCount++;
        }
    }

    // 7. СТАТУС БАР И ФУТЕР
    if (!m_statusMessage.empty() && m_overlayText.empty()) {
        std::string cleanMsg = m_statusMessage;
        cleanMsg.erase(std::remove(cleanMsg.begin(), cleanMsg.end(), '\n'), cleanMsg.end());
        cleanMsg.erase(std::remove(cleanMsg.begin(), cleanMsg.end(), '\r'), cleanMsg.end());
        frame += "\033[K\n" + m_paddingLeft + "\033[38;2;255;180;80mℹ " + cleanMsg + "\033[0m\033[K\n";
        linesCount += 2;
    }

    frame += m_paddingLeft + "\033[38;2;80;80;80m" + std::string(innerWidth, '-') + "\033[0m\033[K\n";
    linesCount++;
    frame += m_paddingLeft + "\033[1;37m[P]\033[0m Play   \033[1;37m[N]\033[0m Next   \033[1;37m[B]\033[0m Prev   \033[1;37m[SH]\033[0m Shuffle   \033[1;37m[V 50]\033[0m Vol   \033[1;37m[H]\033[0m Help   \033[1;37m[Q]\033[0m Quit\033[K";
    linesCount++; // Заметь, в конце НЕТ переноса строки!

    // ==========================================
    // ИДЕАЛЬНАЯ ОТРИСОВКА (Без мерцания и скрытия курсора)
    // ==========================================
    if (m_needsFullRedraw || frame != m_lastPrintedStr) {
        m_lastPrintedStr = frame;
        bool isFullRedraw = m_needsFullRedraw;
        m_needsFullRedraw = false;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [frame, linesCount, isFullRedraw]() {
            std::lock_guard<std::mutex> qLock(Logger::GetMutex());

            if (isFullRedraw) {
                // Если был ресайз окна или нажат Enter - чистим терминал и выделяем место
                printf("\r\033[2J\033[H");
                for (int i = 0; i < linesCount + 2; ++i) printf("\n");
                printf("> ");
            }

            // МАГИЯ: Запоминаем каретку на промпте ввода `> ` (\033[s)
            // Поднимаемся ровно над ним (\033[%dA)
            // Печатаем кадр (он заканчивается прямо над промптом)
            // Возвращаем каретку на промпт (\033[u)
            std::string out = "\033[s";
            out += "\r\033[" + std::to_string(linesCount + 1) + "A";
            out += frame;
            out += "\033[u";

            printf("%s", out.c_str());
            fflush(stdout);
        }, Qt::QueuedConnection);
    }
}