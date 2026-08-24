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

// Градиент в стиле cliamp: сверху теплый оранжево-красный, снизу неоново-зеленый
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

void UltimateRenderer::ReloadConfig() {
    std::lock_guard<std::mutex> lock(m_renderMutex);
    QSettings settings(PathManager::GetCavaConfigPath(), QSettings::IniFormat);

    // Тонкие столбики для стиля cliamp
    m_height = settings.value("Visualizer/Height", 8).toInt();
    m_width = settings.value("Visualizer/Width", 0).toInt();
    m_barWidth = settings.value("Visualizer/BarWidth", 1).toInt();
    m_barSpacing = settings.value("Visualizer/BarSpacing", 0).toInt();

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
    // ВАЖНО: Мы убрали ранний выход, чтобы UI рисовался даже на паузе/при запуске
    std::vector<float> fft;
    if (m_audio.IsPlaying()) {
        fft = m_audio.GetSpectrumData();
    }
    if (fft.empty()) {
        fft.assign(256, 0.0f); // Фейковые нули, чтобы нарисовать пустую спектрограмму
    }

    std::lock_guard<std::mutex> lock(m_renderMutex);

    int consoleWidth, consoleHeight;
    GetConsoleSize(consoleWidth, consoleHeight);
    static int s_lastConsoleWidth = 0;

    if (consoleWidth != s_lastConsoleWidth) {
        if (s_lastConsoleWidth != 0) m_needsFullRedraw = true;
        s_lastConsoleWidth = consoleWidth;
    }

    // --- МАТЕМАТИКА ВЫСОТЫ (ДИНАМИЧЕСКИЙ ПЛЕЙЛИСТ) ---
    // Фиксированные строки: 2(топ) + 1(лого) + 1(титул) + 1(отступ) + 1(время) + 1(отступ)
    // + 1(прогресс) + 1(отступ) + 1(вольюм) + 1(отступ) + 1(шапка_плейлиста) + 1(разделитель_футера) + 1(кнопки) = 14 строк.
    int fixedLines = 14;
    int availableForDynamic = consoleHeight - fixedLines - 1; // 1 строка резервируется под промпт ввода

    int currentHeight = m_height;
    int playlistLines = 5;

    if (availableForDynamic < 2) {
        currentHeight = 1;
        playlistLines = 1;
    } else {
        // Стараемся удержать высоту спектрограммы, отдавая остаток плейлисту
        if (currentHeight > availableForDynamic - 3) {
            currentHeight = availableForDynamic - 3;
        }
        if (currentHeight < 1) currentHeight = 1;
        playlistLines = availableForDynamic - currentHeight;
    }

    // --- МАТЕМАТИКА ШИРИНЫ ---
    int maxInnerChars = consoleWidth - utf8_length(m_paddingLeft) - 4;
    if (maxInnerChars < 20) maxInnerChars = 20;

    int maxBars = maxInnerChars / (m_barWidth + m_barSpacing);
    if (maxBars <= 0) maxBars = 1;

    int currentWidth = m_width;
    if (currentWidth <= 0 || currentWidth > maxBars) currentWidth = maxBars;

    int innerWidth = currentWidth * (m_barWidth + m_barSpacing) - m_barSpacing;
    if (innerWidth < 20) innerWidth = 20;

    std::vector<int> columns(currentWidth, 0);

    for (int x = 0; x < currentWidth; ++x) {
        float peak = 0.0f;
        int startBin = static_cast<int>(std::pow(2.0, x * 7.0 / currentWidth));
        int endBin = static_cast<int>(std::pow(2.0, (x + 1) * 7.0 / currentWidth));

        if (endBin <= startBin) endBin = startBin + 1;
        if (endBin > static_cast<int>(fft.size())) endBin = static_cast<int>(fft.size());

        for (int b = startBin; b < endBin; ++b) {
            if (fft[b] > peak) peak = fft[b];
        }

        int totalTicks = static_cast<int>(std::sqrt(peak) * currentHeight * 8.0f);
        if (totalTicks < 0) totalTicks = 0;
        if (totalTicks > currentHeight * 8) totalTicks = currentHeight * 8;

        columns[x] = totalTicks;
    }

    // ==========================================
    // СБОРКА ПОЛНОГО TUI КАДРА
    // ==========================================
    std::string frame = "";
    int drawnLines = 0;

    // 1. ОТСТУПЫ СВЕРХУ И ШАПКА
    frame += "\033[K\n\033[K\n";
    drawnLines += 2;

    frame += m_paddingLeft + "\033[38;2;80;255;150mV K   Z A E B A L\033[0m\033[K\n";
    drawnLines++;

    // 2. ИНФОРМАЦИЯ О ТРЕКЕ
    Track currentTrack = m_playlist.GetCurrentTrack();
    std::string trackTitle = currentTrack.artist + " - " + currentTrack.title;
    if (trackTitle == " - ") trackTitle = "No Track Loaded";
    trackTitle = safeTruncate(trackTitle, consoleWidth - 20);
    frame += m_paddingLeft + "\033[38;2;250;250;250m🎵 \033[1m" + trackTitle + "\033[0m\033[K\n";
    drawnLines++;

    frame += m_paddingLeft + "\033[K\n"; // Отступ между названием и временем
    drawnLines++;

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
    drawnLines++;

    frame += m_paddingLeft + "\033[K\n"; // Отступ перед спектром
    drawnLines++;

    // 3. ВИЗУАЛИЗАТОР
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
        drawnLines++;
    }

    // 4. ПРОГРЕСС-БАР ПОД СПЕКТРОГРАММОЙ
    int barInnerLength = innerWidth;
    std::string barStr = "";

    if (totalSec > 0.0) {
        int filled = static_cast<int>((currentSec / totalSec) * barInnerLength);
        if (filled > barInnerLength) filled = barInnerLength;

        for (int i = 0; i < barInnerLength; ++i) {
            if (i < filled) barStr += "\033[38;2;80;255;150m━\033[0m"; // Заполненная зеленая линия
            else barStr += "\033[38;2;80;80;80m━\033[0m"; // Пустая серая линия
        }
    } else {
        for (int i = 0; i < barInnerLength; ++i) barStr += "\033[38;2;80;80;80m━\033[0m";
    }
    frame += m_paddingLeft + barStr + "\033[K\n";
    drawnLines++;

    frame += m_paddingLeft + "\033[K\n";
    drawnLines++;

    // 5. ГРОМКОСТЬ И ЭКВАЛАЙЗЕР
    int volPercent = static_cast<int>(std::round(m_audio.GetVolume() * 100));
    int volBars = (volPercent * 25) / 100;
    std::string volStr = "\033[38;2;150;150;150mEQ [\033[38;2;255;180;80m Flat \033[38;2;150;150;150m]     VOL \033[38;2;80;255;150m";

    for(int i = 0; i < 25; i++) {
        if (i < volBars) volStr += "█";
        else volStr += "\033[38;2;80;80;80m▒\033[38;2;80;255;150m";
    }
    volStr += "\033[0m \033[38;2;150;150;150m" + std::to_string(volPercent) + "%\033[K\n";
    frame += m_paddingLeft + volStr;
    drawnLines++;

    frame += m_paddingLeft + "\033[K\n";
    drawnLines++;

    // 6. ДИНАМИЧЕСКИЙ МИНИ-ПЛЕЙЛИСТ
    std::vector<Track> queue = m_playlist.GetQueueTracks();
    int trackIndex = 0;
    for (size_t i = 0; i < queue.size(); ++i) {
        if (queue[i].id == currentTrack.id) { trackIndex = i; break; }
    }

    std::string shufStr = m_playlist.IsShuffle() ? "\033[38;2;255;180;80m[Shuffle]\033[0m" : "\033[38;2;150;150;150m[Ordered]\033[0m";
    int repMode = m_playlist.GetRepeatMode();
    std::string repStr = (repMode == 1) ? "[Repeat: All]" : (repMode == 2 ? "[Repeat: One]" : "[Repeat: Off]");

    frame += m_paddingLeft + "\033[38;2;255;180;80m▶ Playlist\033[0m \033[38;2;100;100;100m—\033[0m " + shufStr + " \033[38;2;150;150;150m" + repStr + " [" + std::to_string(trackIndex + 1) + "/" + std::to_string(queue.size()) + "]\033[0m\033[K\n";
    drawnLines++;

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
        drawnLines++;
    }

    // 7. ЗАПОЛНЕНИЕ ПУСТОТЫ И ФУТЕР
    while (drawnLines < consoleHeight - 3) {
        frame += "\033[K\n";
        drawnLines++;
    }

    frame += m_paddingLeft + "\033[38;2;80;80;80m" + std::string(innerWidth, '-') + "\033[0m\033[K\n";
    drawnLines++;
    frame += m_paddingLeft + "\033[1;37m[P]\033[0m Play   \033[1;37m[N]\033[0m Next   \033[1;37m[B]\033[0m Prev   \033[1;37m[SH]\033[0m Shuffle   \033[1;37m[V 50]\033[0m Vol   \033[1;37m[Q]\033[0m Quit\033[K\n";
    drawnLines++;

    // Заполняем последние оставшиеся строки, чтобы идеально выровнять до низа окна
    while (drawnLines < consoleHeight - 1) {
        frame += "\033[K\n";
        drawnLines++;
    }

    // ==========================================
    // ОТРИСОВКА В АБСОЛЮТНЫХ КООРДИНАТАХ
    // ==========================================
    if (m_needsFullRedraw || frame != m_lastPrintedStr) {
        m_lastPrintedStr = frame;

        bool isFullRedraw = m_needsFullRedraw;
        m_needsFullRedraw = false;

        QMetaObject::invokeMethod(QCoreApplication::instance(), [frame, consoleHeight, isFullRedraw]() {
            std::lock_guard<std::mutex> qLock(Logger::GetMutex());
            std::string out = "\033[?25l"; // Прячем курсор

            if (isFullRedraw) {
                out += "\033[2J"; // Очистка при ресайзе окна
                out += "\033[" + std::to_string(consoleHeight) + ";1H> \033[K";
            }

            out += "\033[s";               // Запоминаем каретку
            out += "\033[H";               // Прыгаем в [0,0]
            out += frame;                  // Рисуем UI
            out += "\033[u";               // Возвращаемся в поле ввода
            out += "\033[?25h";            // Показываем курсор

            printf("%s", out.c_str());
            fflush(stdout);
        }, Qt::QueuedConnection);
    }
}