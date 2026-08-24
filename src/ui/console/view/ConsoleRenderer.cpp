#include "ConsoleRenderer.h"
#include "UltimateRenderer.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include "utils/logger/Logger.h"
#include "utils/path/PathManager.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSettings>
#include <iostream>
#include <cmath>
#include <vector>

ConsoleRenderer::ConsoleRenderer(IAudioEngine& audio, PlaylistManager& playlist)
    : m_audio(audio), m_playlist(playlist) {

    m_cavaRenderer = std::make_unique<UltimateRenderer>(audio, playlist);

    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_mode = settings.value("Visualizer/Mode", 0).toInt();
}

ConsoleRenderer::~ConsoleRenderer() = default;

void ConsoleRenderer::SetVisualizerEnabled(bool enabled) {
    m_showVisualizer = enabled;
}

void ConsoleRenderer::Render() {
    if (m_mode == 1) {
        m_cavaRenderer->Render();
    } else {
        RenderBasic();
    }
}

void ConsoleRenderer::RenderBasic() {
    if (!m_audio.IsPlaying()) return;

    double current = m_audio.GetPositionSeconds();
    if (current < 0.0) current = 0.0;
    int currentSecInt = static_cast<int>(current);

    double total = m_audio.GetLengthSeconds();
    if (total <= 0.0) {
        total = static_cast<double>(m_playlist.GetCurrentTrack().duration);
    }

    if (total > 0.0) {
        int percent = static_cast<int>((current / total) * 100.0);
        if (percent > 100) percent = 100;
        if (percent < 0) percent = 0;

        int curMin = currentSecInt / 60;
        int curSec = currentSecInt % 60;
        int totMin = static_cast<int>(total) / 60;
        int totSec = static_cast<int>(total) % 60;

        int barLength = 40;
        int filled = static_cast<int>((current / total) * barLength);
        if (filled > barLength) filled = barLength;
        if (filled < 0) filled = 0;

        std::string bar = "[";
        for (int i = 0; i < barLength; ++i) {
            if (i < filled) bar += "\xE2\x96\x88";
            else bar += "-";
        }
        bar += "]";

        std::string spectrum = " [";
        const int numBands = 16;
        if (m_showVisualizer) {
            const char* blocks[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
            std::vector<float> fft = m_audio.GetSpectrumData();
            if (!fft.empty()) {
                for (int i = 0; i < numBands; ++i) {
                    float peak = 0.0f;
                    int startBin = static_cast<int>(std::pow(2.0, i * 7.0 / numBands));
                    int endBin = static_cast<int>(std::pow(2.0, (i + 1) * 7.0 / numBands));

                    if (endBin <= startBin) endBin = startBin + 1;
                    if (endBin > static_cast<int>(fft.size())) endBin = static_cast<int>(fft.size());

                    for (int b = startBin; b < endBin; ++b) {
                        if (fft[b] > peak) peak = fft[b];
                    }

                    int level = static_cast<int>(std::sqrt(peak) * 18.0f);
                    if (level < 0) level = 0;
                    if (level > 7) level = 7;

                    spectrum += blocks[level];
                }
            } else {
                spectrum += std::string(numBands, ' ');
            }
        } else {
            spectrum += std::string(numBands, ' ');
        }
        spectrum += "]";

        Track currentTrack = m_playlist.GetCurrentTrack();
        std::vector<Track> queue = m_playlist.GetQueueTracks();
        int trackIndex = 0;

        for (size_t i = 0; i < queue.size(); ++i) {
            if (queue[i].id == currentTrack.id) {
                trackIndex = i + 1;
                break;
            }
        }

        std::string trackName = std::to_string(trackIndex) + ". " + currentTrack.artist + " - " + currentTrack.title;

        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s | %02d:%02d / %02d:%02d %s %d%%%s",
                 trackName.c_str(), curMin, curSec, totMin, totSec, bar.c_str(), percent, spectrum.c_str());
        std::string currentStr = buffer;

        if (currentStr != m_lastPrintedStr) {
            m_lastPrintedStr = currentStr;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [currentStr]() {
                std::lock_guard<std::mutex> lock(Logger::GetMutex());
                printf("\033[s\033[1A\r\033[2K%s\033[u", currentStr.c_str());
                fflush(stdout);
            }, Qt::QueuedConnection);
        }
    }
}

void ConsoleRenderer::ReloadConfig() {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    m_mode = settings.value("Visualizer/Mode", 0).toInt();

    m_lastPrintedStr = "";
    m_cavaRenderer->ReloadConfig();
}