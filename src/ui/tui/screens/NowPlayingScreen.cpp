#include "NowPlayingScreen.h"
#include "ui/tui/utils/TuiStringUtils.h"
#include "ui/tui/TuiTheme.h"
#include "ui/tui/CoverArtRenderer.h"
#include "ui/tui/components/SidebarComponent.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"

#include <ftxui/dom/canvas.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace tui {

using utils::FormatTime;
using utils::ScrollText;

namespace {
class TransparentCanvasNode : public ftxui::Node {
public:
    explicit TransparentCanvasNode(ftxui::Canvas canvas) : m_canvas(std::move(canvas)) {
        requirement_.min_x = (m_canvas.width() + 1) / 2;
        requirement_.min_y = (m_canvas.height() + 3) / 4;
    }
    void ComputeRequirement() override {
        requirement_.min_x = (m_canvas.width() + 1) / 2;
        requirement_.min_y = (m_canvas.height() + 3) / 4;
    }
    void Render(ftxui::Screen& screen) override {
        const int y_max = std::min(m_canvas.height() / 4, box_.y_max - box_.y_min + 1);
        const int x_max = std::min(m_canvas.width() / 2, box_.x_max - box_.x_min + 1);
        for (int y = 0; y < y_max; ++y) {
            for (int x = 0; x < x_max; ++x) {
                auto& target = screen.PixelAt(box_.x_min + x, box_.y_min + y);
                auto src = m_canvas.GetPixel(x, y);
                target.character = src.character;
                target.foreground_color = src.foreground_color;
                if (src.background_color != ftxui::Color::Default) {
                    target.background_color = src.background_color;
                }
            }
        }
    }
private:
    ftxui::Canvas m_canvas;
};

inline ftxui::Element TransparentCanvas(ftxui::Canvas c) {
    return std::make_shared<TransparentCanvasNode>(std::move(c));
}
} // namespace

NowPlayingScreen::NowPlayingScreen(IAudioEngine& audio,
                                   PlaylistManager& playlist,
                                   CoverArtRenderer& coverRenderer,
                                   std::shared_ptr<SidebarComponent> sidebar)
    : m_audio(audio),
      m_playlist(playlist),
      m_coverRenderer(coverRenderer),
      m_sidebar(std::move(sidebar)),
      m_theme(GetDefaultTheme()) {
    m_spectrumPeaks.assign(kNumBars, 0.0f);

    m_visConfig.colorPeak = ftxui::Color::RGB(255, 123, 114);
    m_visConfig.gradientStops = {
        {50, 255, 150},
        {255, 184, 108},
        {255, 80, 80}
    };
}

void NowPlayingScreen::UpdateTheme(const ThemePalette& palette, const VisualizerConfig& visConfig) {
    m_theme = palette;
    m_visConfig = visConfig;
    m_visMode = static_cast<VisualizerMode>(std::clamp(visConfig.mode, 0, 2));
}

void NowPlayingScreen::UpdateSpectrum(const std::vector<float>& fft) {
    if (fft.empty()) {
        for (size_t x = 0; x < kNumBars; ++x) {
            m_spectrumPeaks[x] *= 0.78f;
            if (m_spectrumPeaks[x] < 0.002f) {
                m_spectrumPeaks[x] = 0.0f;
            }
        }
        return;
    }

    m_sinePhase += 0.18f;
    if (m_sinePhase > 628.3185f) m_sinePhase -= 628.3185f;

    // Logarithmic frequency distribution (35Hz - 16000Hz) with ISO 226 / Pink noise compensation
    for (int x = 0; x < kNumBars; ++x) {
        float norm = static_cast<float>(x) / static_cast<float>(kNumBars - 1);
        float freq = 35.0f * std::pow(16000.0f / 35.0f, norm);
        float centerBin = freq / 172.26f;

        int b0 = std::clamp(static_cast<int>(centerBin), 1, static_cast<int>(fft.size()) - 1);
        int b1 = std::min(static_cast<int>(fft.size()) - 1, b0 + 1);
        float frac = centerBin - b0;
        float rawMag = fft[b0] * (1.0f - frac) + fft[b1] * frac;

        // Equal loudness weighting: attenuate booming sub-bass, boost quiet highs
        float weight = 0.35f + 0.95f * norm;
        float weightedVal = rawMag * weight * m_visConfig.sensitivity;
        float targetAmp = std::clamp(std::sqrt((std::max)(0.0f, weightedVal)), 0.0f, 1.0f);

        // Smooth attack / release
        float smoothVal = m_visConfig.smoothing;
        if (targetAmp > m_spectrumPeaks[x]) {
            m_spectrumPeaks[x] = targetAmp * 0.75f + m_spectrumPeaks[x] * 0.25f;
        } else {
            m_spectrumPeaks[x] = m_spectrumPeaks[x] * smoothVal + targetAmp * (1.0f - smoothVal);
        }
    }
}

bool NowPlayingScreen::HasActiveSpectrum() const {
    for (float v : m_spectrumPeaks) {
        if (v > 0.001f) return true;
    }
    return false;
}

void NowPlayingScreen::SetStatusMessage(const std::string& msg) {
    m_statusMessage = msg;
}

ftxui::Element NowPlayingScreen::RenderTopBlock() {
    auto theme = m_theme;
    Track currentTrack = m_playlist.GetCurrentTrack();

    std::string trackTitle = currentTrack.artist.empty() ? "No Track Loaded" : (currentTrack.artist + " — " + currentTrack.title);
    std::string sourceName = currentTrack.source.empty() ? "VK" : currentTrack.source;
    ftxui::Color serviceColor = GetServiceColor(sourceName);
    std::string sourceBadge = FormatSourceShort(sourceName);

    // Terminal width for visualizer & scrolling
    int termWidth = ftxui::Terminal::Size().dimx;
    if (termWidth <= 0) termWidth = 100;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (winW > 20) {
            termWidth = winW;
        }
    }
#endif

    bool hasSidebar = (m_sidebar && m_sidebar->IsVisible());

    int availVisWidth;
    int titleMaxCols;
    if (hasSidebar) {
        availVisWidth = std::max(20, termWidth - 53);
        titleMaxCols = std::max(20, availVisWidth - 4);
    } else {
        availVisWidth = std::clamp(termWidth - 36, 20, std::max(80, termWidth - 36));
        titleMaxCols = std::max(20, availVisWidth - 6);
    }

    std::string scrolledTitle = ScrollText(trackTitle, titleMaxCols, m_tickerTick);

    bool isPlaying = m_audio.IsPlaying();
    std::string playStatus = isPlaying ? "▶ Playing" : "■ Paused";
    ftxui::Color statusColor = isPlaying ? theme.accent : theme.textMuted;

    // Row 1: Source badge + Track Title
    ftxui::Element titleRow = ftxui::hbox({
        ftxui::text(" "),
        ftxui::text(sourceBadge) | ftxui::bold | ftxui::color(serviceColor),
        ftxui::text(" "),
        ftxui::text(scrolledTitle) | ftxui::bold | ftxui::color(theme.text),
        ftxui::filler()
    });

    // Row 2: Status
    std::vector<ftxui::Element> statusElements;
    statusElements.push_back(ftxui::text(" "));
    statusElements.push_back(ftxui::text(playStatus) | ftxui::bold | ftxui::color(statusColor));
    if (!m_isOnline) {
        statusElements.push_back(ftxui::text("  "));
        statusElements.push_back(ftxui::text("[OFFLINE]") | ftxui::bold | ftxui::color(theme.accentRed));
    }
    if (!m_statusMessage.empty()) {
        statusElements.push_back(ftxui::filler());
        statusElements.push_back(ftxui::text(m_statusMessage + " ") | ftxui::bold | ftxui::color(theme.accentOrange));
    } else {
        statusElements.push_back(ftxui::filler());
    }
    ftxui::Element statusRow = ftxui::hbox(std::move(statusElements));

    const int kVisHeight = 11;
    ftxui::Element visualizerElem = RenderVisualizer(availVisWidth, kVisHeight);

    // Cover Art height: 14 rows, matching rightInfo total height (1 + 1 + 1 + 11 = 14)
    // Both cover art and visualizer bottom edges align perfectly!
    ftxui::Element coverElem = m_coverRenderer.Render(22, 14);

    ftxui::Element rightInfo = ftxui::vbox({
        std::move(titleRow),
        std::move(statusRow),
        ftxui::text(""),
        std::move(visualizerElem)
    });

    if (hasSidebar) {
        return ftxui::hbox({
            ftxui::text(" "),
            std::move(coverElem),
            ftxui::text(" "),
            std::move(rightInfo) | ftxui::flex
        });
    }

    // When sidebar is hidden/collapsed: center the entire top block with equal left and right margins (X)
    return ftxui::hbox({
        ftxui::filler(),
        std::move(coverElem),
        ftxui::text("  "),
        std::move(rightInfo) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, availVisWidth),
        ftxui::filler()
    });
}

void NowPlayingScreen::CycleVisualizerMode() {
    int nextMode = (static_cast<int>(m_visMode) + 1) % 3;
    m_visMode = static_cast<VisualizerMode>(nextMode);
    // Visualizer type label completely removed as requested
}

ftxui::Element NowPlayingScreen::RenderVisualizer(int availCols, int availRows) {
    if (availCols <= 0 || availRows <= 0) {
        return ftxui::text("");
    }
    if (!HasActiveSpectrum()) {
        return ftxui::vbox({
            ftxui::filler()
        }) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, availRows) | ftxui::reflect(m_visBox);
    }

    ftxui::Element elem;
    switch (m_visMode) {
        case VisualizerMode::SOLID_BARS:
            elem = RenderSolidBars(availCols, availRows);
            break;
        case VisualizerMode::SYMMETRIC_BARS:
            elem = RenderSymmetricBars(availCols, availRows);
            break;
        case VisualizerMode::SILK_WAVE:
            elem = RenderSilkWave(availCols, availRows);
            break;
    }
    return elem | ftxui::reflect(m_visBox);
}

ftxui::Element NowPlayingScreen::RenderSolidBars(int availCols, int availRows) {
    static const char* kSubBlocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    int barW = std::clamp(m_visConfig.barWidth, 1, 2);
    int barSp = std::clamp(m_visConfig.barSpacing, 0, 1);
    int step = barW + barSp;
    int numBars = std::max(4, availCols / step);

    int totalLevels = availRows * 8;

    struct BarInfo {
        int subUnits = 0;
    };
    std::vector<BarInfo> bars(numBars);

    for (int i = 0; i < numBars; ++i) {
        float normX = static_cast<float>(i) / static_cast<float>(std::max(1, numBars - 1));
        float binPos = normX * (kNumBars - 1);
        int b0 = static_cast<int>(binPos);
        int b1 = std::min(kNumBars - 1, b0 + 1);
        float frac = binPos - b0;

        float amp = m_spectrumPeaks[b0] * (1.0f - frac) + m_spectrumPeaks[b1] * frac;
        amp *= m_visConfig.sensitivity;
        amp = std::clamp(amp, 0.0f, 1.0f);

        bars[i].subUnits = std::clamp(static_cast<int>(amp * totalLevels), 0, totalLevels);
    }

    std::vector<ftxui::Element> rows;
    rows.reserve(availRows);

    for (int r = 0; r < availRows; ++r) {
        int rowFromBottom = (availRows - 1) - r;
        int minSub = rowFromBottom * 8;
        int maxSub = minSub + 8;

        // Vertical gradient color based on row height from bottom to top
        float levelNorm = static_cast<float>(rowFromBottom) / static_cast<float>(std::max(1, availRows - 1));
        ftxui::Color rowColor = InterpolateColor(levelNorm, m_visConfig.gradientStops);

        std::vector<ftxui::Element> rowElements;
        rowElements.reserve(numBars * 2);

        for (int i = 0; i < numBars; ++i) {
            const auto& bar = bars[i];
            std::string cellChar;

            if (bar.subUnits >= maxSub) {
                cellChar = "█";
            } else if (bar.subUnits > minSub) {
                int fracIdx = bar.subUnits - minSub;
                cellChar = kSubBlocks[std::clamp(fracIdx, 0, 8)];
            } else {
                cellChar = " ";
            }

            std::string barStr;
            for (int w = 0; w < barW; ++w) {
                barStr += cellChar;
            }
            rowElements.push_back(ftxui::text(barStr) | ftxui::color(rowColor));

            if (barSp > 0) {
                rowElements.push_back(ftxui::text(" "));
            }
        }

        rows.push_back(ftxui::hbox(std::move(rowElements)));
    }

    return ftxui::vbox(std::move(rows));
}

ftxui::Element NowPlayingScreen::RenderSymmetricBars(int availCols, int availRows) {
    static const char* kSubBlocks[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    int barW = std::clamp(m_visConfig.barWidth, 1, 2);
    int barSp = std::clamp(m_visConfig.barSpacing, 0, 1);
    int step = barW + barSp;
    int numBars = std::max(4, availCols / step);

    int midRow = availRows / 2;
    int maxHalfRows = std::max(1, midRow);
    int totalLevels = maxHalfRows * 8;

    struct SymBarInfo {
        int subUnits = 0;
    };
    std::vector<SymBarInfo> bars(numBars);

    for (int i = 0; i < numBars; ++i) {
        float normX = static_cast<float>(i) / static_cast<float>(std::max(1, numBars - 1));
        float binPos = normX * (kNumBars - 1);
        int b0 = static_cast<int>(binPos);
        int b1 = std::min(kNumBars - 1, b0 + 1);
        float frac = binPos - b0;

        float amp = m_spectrumPeaks[b0] * (1.0f - frac) + m_spectrumPeaks[b1] * frac;
        amp *= m_visConfig.sensitivity;
        amp = std::clamp(amp, 0.0f, 1.0f);
        bars[i].subUnits = std::clamp(static_cast<int>(amp * totalLevels), 0, totalLevels);
    }

    std::vector<ftxui::Element> rows;
    rows.reserve(availRows);

    for (int r = 0; r < availRows; ++r) {
        int distFromMid = std::abs(r - midRow);
        float levelNorm = static_cast<float>(distFromMid) / static_cast<float>(maxHalfRows);
        ftxui::Color rowColor = InterpolateColor(levelNorm, m_visConfig.gradientStops);

        std::vector<ftxui::Element> rowElements;
        rowElements.reserve(numBars * 2);

        for (int i = 0; i < numBars; ++i) {
            const auto& bar = bars[i];
            std::string cellChar;

            if (r == midRow) {
                cellChar = (bar.subUnits > 0) ? "█" : " ";
            } else if (r < midRow) {
                int d = midRow - r;
                int minSub = (d - 1) * 8;
                int maxSub = d * 8;

                if (bar.subUnits >= maxSub) {
                    cellChar = "█";
                } else if (bar.subUnits > minSub) {
                    int fracIdx = bar.subUnits - minSub;
                    cellChar = kSubBlocks[std::clamp(fracIdx, 0, 8)];
                } else {
                    cellChar = " ";
                }
            } else {
                int d = r - midRow;
                int minSub = (d - 1) * 8;
                int maxSub = d * 8;

                if (bar.subUnits >= maxSub) {
                    cellChar = "█";
                } else if (bar.subUnits > minSub) {
                    int fracIdx = bar.subUnits - minSub;
                    if (fracIdx >= 5) {
                        cellChar = "█";
                    } else if (fracIdx >= 2) {
                        cellChar = "▀";
                    } else {
                        cellChar = "▔";
                    }
                } else {
                    cellChar = " ";
                }
            }

            std::string barStr;
            for (int w = 0; w < barW; ++w) barStr += cellChar;
            rowElements.push_back(ftxui::text(barStr) | ftxui::color(rowColor));
            if (barSp > 0) rowElements.push_back(ftxui::text(" "));
        }
        rows.push_back(ftxui::hbox(std::move(rowElements)));
    }

    return ftxui::vbox(std::move(rows));
}

ftxui::Element NowPlayingScreen::RenderSilkWave(int availCols, int availRows) {
    int canvasWidth = availCols * 2;
    int canvasHeight = availRows * 4;
    auto c = ftxui::Canvas(canvasWidth, canvasHeight);

    float midY = (canvasHeight - 1) / 2.0f;
    float maxAmp = (midY - 1.0f);

    int prevUpper = static_cast<int>(midY);
    int prevLower = static_cast<int>(midY);

    for (int x = 0; x < canvasWidth; ++x) {
        float normX = static_cast<float>(x) / static_cast<float>(std::max(1, canvasWidth - 1));

        float rawSin = std::sin(normX * 3.14159265f);
        float edgeWindow = (rawSin > 0.0f) ? std::pow(rawSin, 0.45f) : 0.0f;

        float binPos = normX * (kNumBars - 1);
        int b0 = static_cast<int>(binPos);
        int b1 = std::min(kNumBars - 1, b0 + 1);
        float frac = binPos - b0;
        float smoothFrac = 0.5f * (1.0f - std::cos(frac * 3.14159265f));
        float rawVal = m_spectrumPeaks[b0] * (1.0f - smoothFrac) + m_spectrumPeaks[b1] * smoothFrac;

        float amp = rawVal * m_visConfig.sensitivity;
        amp = std::clamp(amp, 0.0f, 1.0f) * edgeWindow;

        float ripple = 0.08f * std::sin(normX * 10.0f + m_sinePhase * 2.0f);
        float dynamicAmp = std::clamp(amp + ripple * amp, 0.0f, 1.0f);

        int upperY = std::clamp(static_cast<int>(std::round(midY - dynamicAmp * maxAmp)), 0, canvasHeight - 1);
        int lowerY = std::clamp(static_cast<int>(std::round(midY + dynamicAmp * maxAmp)), 0, canvasHeight - 1);

        ftxui::Color upperColor = InterpolateColor(dynamicAmp, m_visConfig.gradientStops);
        ftxui::Color centerColor = (m_visConfig.gradientStops.empty()) ? ftxui::Color::White :
                                   ftxui::Color::RGB(m_visConfig.gradientStops[0].r, m_visConfig.gradientStops[0].g, m_visConfig.gradientStops[0].b);

        if (x > 0) {
            c.DrawPointLine(x - 1, prevUpper, x, upperY, upperColor);
            c.DrawPointLine(x - 1, prevLower, x, lowerY, upperColor);
            c.DrawPointLine(x - 1, static_cast<int>(midY), x, static_cast<int>(midY), centerColor);
        } else {
            c.DrawPoint(x, upperY, true, upperColor);
            c.DrawPoint(x, lowerY, true, upperColor);
            c.DrawPoint(x, static_cast<int>(midY), true, centerColor);
        }

        if (dynamicAmp > 0.15f) {
            int innerUpper = static_cast<int>(midY - dynamicAmp * maxAmp * 0.5f);
            int innerLower = static_cast<int>(midY + dynamicAmp * maxAmp * 0.5f);
            ftxui::Color midColor = InterpolateColor(dynamicAmp * 0.5f, m_visConfig.gradientStops);
            c.DrawPoint(x, innerUpper, true, midColor);
            c.DrawPoint(x, innerLower, true, midColor);
        }

        prevUpper = upperY;
        prevLower = lowerY;
    }

    return TransparentCanvas(std::move(c));
}

ftxui::Element NowPlayingScreen::RenderControls() {
    auto theme = m_theme;
    double currentSec = m_audio.GetPositionSeconds();
    double totalSec = m_audio.GetLengthSeconds();
    if (totalSec <= 0.0) {
        totalSec = static_cast<double>(m_playlist.GetCurrentTrack().duration);
    }
    if (currentSec < 0.0) currentSec = 0.0;
    if (totalSec < 0.0) totalSec = 0.0;

    float progress = (totalSec > 0.0) ? static_cast<float>(currentSec / totalSec) : 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);

    // Progress Bar Track
    int termWidth = ftxui::Terminal::Size().dimx;
    if (termWidth <= 0) termWidth = 100;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (winW > 20) {
            termWidth = winW;
        }
    }
#endif

    bool hasSidebar = (m_sidebar && m_sidebar->IsVisible());

    std::string timeStr = FormatTime(currentSec) + " / " + FormatTime(totalSec);
    int timePartWidth = static_cast<int>(timeStr.size()) + 2;

    int trackWidth;
    if (hasSidebar) {
        trackWidth = std::clamp(termWidth - 44, 20, 120);
    } else {
        // POINT 8: Total progress bar width matches top panel width (cover + 2 + vis)
        int availVisWidth = std::clamp(termWidth - 36, 20, std::max(80, termWidth - 36));
        int totalTopWidth = 22 + 2 + availVisWidth;
        trackWidth = std::max(20, totalTopWidth - timePartWidth);
    }

    int filledWidth = static_cast<int>(progress * trackWidth);
    std::string playedStr;
    std::string thumbStr;
    std::string remainStr;
    for (int i = 0; i < trackWidth; ++i) {
        if (i < filledWidth) playedStr += "━";
        else if (i == filledWidth) thumbStr += "●";
        else remainStr += "━";
    }

    std::vector<ftxui::Element> barParts;
    if (!playedStr.empty()) barParts.push_back(ftxui::text(playedStr) | ftxui::color(theme.progressPlayed));
    if (!thumbStr.empty()) barParts.push_back(ftxui::text(thumbStr) | ftxui::bold | ftxui::color(theme.progressThumb));
    if (!remainStr.empty()) barParts.push_back(ftxui::text(remainStr) | ftxui::color(theme.progressRemaining));

    ftxui::Element progressBarElem;
    if (hasSidebar) {
        progressBarElem = ftxui::hbox({
            ftxui::text(" "),
            ftxui::hbox(std::move(barParts)) | ftxui::reflect(m_seekBarBox),
            ftxui::text("  "),
            ftxui::text(timeStr) | ftxui::color(theme.textMuted),
            ftxui::text(" ")
        });
    } else {
        progressBarElem = ftxui::hbox({
            ftxui::filler(),
            ftxui::hbox(std::move(barParts)) | ftxui::reflect(m_seekBarBox),
            ftxui::text("  "),
            ftxui::text(timeStr) | ftxui::color(theme.textMuted),
            ftxui::filler()
        });
    }

    // Control Buttons
    bool isPlaying = m_audio.IsPlaying();
    std::string playPauseIcon = isPlaying ? " [PAUSE] " : " [PLAY] ";

    ftxui::Element prevBtn = ftxui::text(" [<<] ")
        | ftxui::color(theme.text)
        | ftxui::reflect(m_prevBtnBox);

    ftxui::Element playPauseBtn = ftxui::text(playPauseIcon)
        | ftxui::bold
        | ftxui::color(theme.accent)
        | ftxui::reflect(m_playPauseBtnBox);

    ftxui::Element nextBtn = ftxui::text(" [>>] ")
        | ftxui::color(theme.text)
        | ftxui::reflect(m_nextBtnBox);

    std::string heartGlyph = m_isCurrentTrackLiked ? " ♥ " : " ♡ ";
    ftxui::Element likeBtn = ftxui::text(heartGlyph)
        | (m_isCurrentTrackLiked ? ftxui::color(theme.accentRed) : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_likeBtnBox);

    bool isShuffle = m_playlist.IsShuffle();
    ftxui::Element shuffleBtn = ftxui::text(isShuffle ? " [Shuffle] " : " [Ordered] ")
        | (isShuffle ? ftxui::color(theme.accentOrange) : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_shuffleBtnBox);

    int repMode = m_playlist.GetRepeatMode();
    std::string repLabel = (repMode == 1) ? " [Repeat: All] " : ((repMode == 2) ? " [Repeat: One] " : " [Repeat: Off] ");
    ftxui::Element repeatBtn = ftxui::text(repLabel)
        | (repMode > 0 ? ftxui::color(theme.accentPurple) : ftxui::color(theme.textMuted))
        | ftxui::reflect(m_repeatBtnBox);

    // Volume & EQ
    int volPercent = static_cast<int>(std::round(m_audio.GetVolume() * 100.0f));
    int volBars = std::clamp((volPercent * 20) / 100, 0, 20);
    std::string volBarStr;
    for (int i = 0; i < 20; ++i) {
        if (i < volBars) volBarStr += "█";
        else volBarStr += "▒";
    }

    ftxui::Element volElem = ftxui::hbox({
        ftxui::text("[-] ") | ftxui::bold | ftxui::color(theme.textMuted) | ftxui::reflect(m_volMinusBox),
        ftxui::text("VOL ") | ftxui::color(theme.textMuted),
        ftxui::text(volBarStr) | ftxui::color(theme.accent),
        ftxui::text(" " + std::to_string(volPercent) + "%") | ftxui::color(theme.textMuted),
        ftxui::text(" [+]") | ftxui::bold | ftxui::color(theme.textMuted) | ftxui::reflect(m_volPlusBox)
    });

    bool eqOn = m_audio.IsEqualizerEnabled();
    std::string eqPreset = m_audio.GetEqualizerPreset();
    std::string eqText = eqOn ? ("EQ [ " + eqPreset + " ]") : "EQ [ OFF ]";
    auto eqColor = eqOn ? theme.accentOrange : theme.textMuted;
    ftxui::Element eqElem = ftxui::text(eqText) | ftxui::bold | ftxui::color(eqColor) | ftxui::reflect(m_eqBox);

    ftxui::Element buttonsRow = ftxui::hbox({
        ftxui::text(" "),
        prevBtn,
        playPauseBtn,
        nextBtn,
        shuffleBtn,
        repeatBtn,
        ftxui::filler(),
        eqElem,
        ftxui::text("     "),
        volElem,
        ftxui::text(" ")
    });

    return ftxui::vbox({
        std::move(progressBarElem),
        ftxui::text(""),
        std::move(buttonsRow)
    });
}

ftxui::Element NowPlayingScreen::RenderQueue() {
    auto theme = m_theme;
    size_t totalTracks = m_playlist.GetQueueSize();
    Track currentTrack = m_playlist.GetCurrentTrack();

    std::vector<ftxui::Element> rows;

    std::string shufStr = m_playlist.IsShuffle() ? "[Shuffle]" : "[Ordered]";
    int repMode = m_playlist.GetRepeatMode();
    std::string repStr = (repMode == 1) ? "[Repeat: All]" : ((repMode == 2) ? "[Repeat: One]" : "[Repeat: Off]");

    int currentIdx = m_playlist.GetCurrentQueueIndex();

    std::string queueHeader = "▶ Playlist — " + shufStr + " " + repStr + " [" +
                              std::to_string(totalTracks == 0 ? 0 : currentIdx + 1) + "/" +
                              std::to_string(totalTracks) + "]";

    rows.push_back(
        ftxui::hbox({
            ftxui::text(" " + queueHeader) | ftxui::bold | ftxui::color(theme.accentOrange),
            ftxui::filler()
        })
    );
    rows.push_back(ftxui::text(""));

    // Unauthorized source placeholder
    if (!m_isSourceAuthorized && m_activeSource.rfind("Custom:", 0) != 0 && m_activeSource != "Offline" && m_activeSource != "All") {
        std::string serviceTitle = m_activeSource;
        if (serviceTitle == "VK") serviceTitle = "VKontakte";
        else if (serviceTitle == "Spotify") serviceTitle = "Spotify";
        else if (serviceTitle == "Yandex") serviceTitle = "Яндекс Музыка";
        else if (serviceTitle == "SoundCloud") serviceTitle = "SoundCloud";
        else if (serviceTitle == "YouTube") serviceTitle = "YouTube Music";

        auto titleElem = ftxui::text(" СЕРВИС: " + serviceTitle + " ") | ftxui::bold | ftxui::color(theme.accentOrange) | ftxui::center;
        auto subtitleElem = ftxui::text("Требуется вход в учетную запись") | ftxui::color(theme.textMuted) | ftxui::center;

        auto loginBtnElem = ftxui::text(" [  Войти в аккаунт  ] ") | ftxui::bold;
        if (m_isLoginBtnHovered) {
            loginBtnElem = loginBtnElem | ftxui::color(theme.bg) | ftxui::bgcolor(theme.accent);
        } else {
            loginBtnElem = loginBtnElem | ftxui::color(theme.accent) | ftxui::bgcolor(theme.cardBg);
        }
        auto loginBtnBox = loginBtnElem | ftxui::center | ftxui::reflect(m_loginBtnBox);

        auto card = ftxui::vbox({
            ftxui::separatorEmpty(),
            titleElem,
            ftxui::separatorEmpty(),
            subtitleElem,
            ftxui::separatorEmpty(),
            loginBtnBox,
            ftxui::separatorEmpty()
        }) | ftxui::borderRounded | ftxui::color(theme.border) | ftxui::bgcolor(theme.panelBg) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50) | ftxui::center;

        return ftxui::vbox({
            ftxui::filler(),
            card,
            ftxui::filler()
        }) | ftxui::flex;
    }

    if (totalTracks == 0) {
        rows.push_back(
            ftxui::text("  Плейлист пуст. Выберите сервис или используйте поиск [F]")
            | ftxui::color(theme.textMuted)
        );
        rows.push_back(ftxui::filler());
        return ftxui::vbox(std::move(rows)) | ftxui::flex;
    }

    // Dynamic row calculation based on terminal height to fill the entire remaining block
    int termHeight = ftxui::Terminal::Size().dimy;
    if (termHeight <= 0) termHeight = 30;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        if (winH > 10) {
            termHeight = winH;
        }
    }
#endif
    int reservedLines = m_showBottomBar ? 28 : 25;
    int availTrackRows = std::max(2, termHeight - reservedLines);
    int maxDisplayRows = std::max(2, availTrackRows / 2);
    m_visibleQueueRows = maxDisplayRows;

    int termWidth = ftxui::Terminal::Size().dimx;
    if (termWidth <= 0) termWidth = 100;
#ifdef _WIN32
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (winW > 20) {
            termWidth = winW;
        }
    }
#endif

    int total = static_cast<int>(totalTracks);
    if (m_queueCursor < 0) m_queueCursor = 0;
    if (m_queueCursor >= total) m_queueCursor = std::max(0, total - 1);

    int maxScroll = std::max(0, total - maxDisplayRows);

    // Auto-scroll logic: only when total > maxDisplayRows and active track changes
    bool trackChanged = (currentTrack.id != m_lastActiveTrackId || currentIdx != m_lastActiveTrackIndex);
    if (trackChanged) {
        m_lastActiveTrackId = currentTrack.id;
        m_lastActiveTrackIndex = currentIdx;
        if (m_autoScroll && total > maxDisplayRows && currentIdx >= 0 && currentIdx < total) {
            m_scrollOffset = std::clamp(currentIdx - maxDisplayRows / 2, 0, maxScroll);
            m_queueCursor = currentIdx;
        }
    }

    if (total <= maxDisplayRows) {
        m_scrollOffset = 0;
    } else {
        m_scrollOffset = std::clamp(m_scrollOffset, 0, maxScroll);
    }

    int endIndex = std::min(total, m_scrollOffset + maxDisplayRows);
    int sliceCount = std::max(0, endIndex - m_scrollOffset);

    auto visibleTracks = m_playlist.GetQueueSlice(m_scrollOffset, sliceCount);

    m_visibleTrackIndices.clear();
    m_queueRowBoxes.clear();
    m_queueRowBoxes.resize(visibleTracks.size());
    m_queueLikeBoxes.clear();
    m_queueLikeBoxes.resize(visibleTracks.size());
    m_queueAddBoxes.clear();
    m_queueAddBoxes.resize(visibleTracks.size());
    m_queueDlBoxes.clear();
    m_queueDlBoxes.resize(visibleTracks.size());

    bool showSource = false;
    if (m_sidebar) {
        showSource = (m_sidebar->IsSelectedPlaylist() || m_sidebar->GetSelectedId() == "All");
    }

    int metaCols = std::clamp(termWidth - (showSource ? 65 : 58), 20, 70);

    for (size_t rowIdx = 0; rowIdx < visibleTracks.size(); ++rowIdx) {
        int i = m_scrollOffset + static_cast<int>(rowIdx);
        m_visibleTrackIndices.push_back(i);

        const auto& t = visibleTracks[rowIdx];
        bool isCurrent = (t.id == currentTrack.id);
        bool isSelected = (i == m_queueCursor);
        bool isHovered = (static_cast<int>(rowIdx) == m_hoveredQueueRow);
        bool isLikeHovered = (static_cast<int>(rowIdx) == m_hoveredLikeRow);
        bool isAddHovered = (static_cast<int>(rowIdx) == m_hoveredAddRow);
        bool isDlHovered = (static_cast<int>(rowIdx) == m_hoveredDlRow);

        std::string numStr = std::to_string(i + 1) + ".";
        while (numStr.length() < 4) numStr = " " + numStr;

        std::string playIndicator = isCurrent ? "▶ " : "  ";

        ftxui::Color rowTextColor = theme.text;
        if (isCurrent) rowTextColor = theme.accent;
        else if (isSelected) rowTextColor = ftxui::Color::White;

        std::vector<ftxui::Element> rowLeft;
        rowLeft.push_back(ftxui::text(" " + playIndicator) | ftxui::bold | ftxui::color(isCurrent ? theme.accent : theme.textMuted));
        rowLeft.push_back(ftxui::text(numStr) | ftxui::color(theme.textMuted));
        rowLeft.push_back(ftxui::text(" "));
        if (showSource) {
            std::string sourceBadge = FormatSourceShort(t.source.empty() ? "VK" : t.source);
            rowLeft.push_back(ftxui::text(sourceBadge) | ftxui::bold | ftxui::color(GetServiceColor(t.source)));
            rowLeft.push_back(ftxui::text(" "));
        }

        ftxui::Element metaElem;
        std::string srcLower = t.source.empty() ? (m_sidebar ? m_sidebar->GetSelectedId() : "") : t.source;
        for (char& c : srcLower) c = std::tolower(c);
        bool isSoundCloud = (srcLower == "soundcloud" || srcLower == "sc");
        if (isSoundCloud) {
            std::string scText = t.title.empty() ? t.artist : t.title;
            scText = ScrollText(scText, metaCols, m_tickerTick);
            metaElem = ftxui::text(scText) | ftxui::bold | ftxui::color(rowTextColor);
        } else {
            std::string titleText = t.title.empty() ? (t.artist.empty() ? "Без названия" : t.artist) : t.title;
            std::string artistText = t.artist.empty() ? "—" : t.artist;
            titleText = ScrollText(titleText, metaCols, m_tickerTick);
            artistText = ScrollText(artistText, metaCols, m_tickerTick);
            metaElem = ftxui::vbox({
                ftxui::text(titleText) | ftxui::bold | ftxui::color(rowTextColor),
                ftxui::text(artistText) | ftxui::color(theme.textMuted)
            });
        }

        // Action Buttons: Like, Add to playlist, Download
        bool isLiked = (m_favoriteTrackIds.count(t.id) > 0);
        std::string heartGlyph = isLiked ? " ♥ " : " ♡ ";

        ftxui::Element likeBtn = ftxui::text(heartGlyph)
            | (isLiked ? ftxui::color(theme.accentRed) : ftxui::color(theme.textMuted));
        if (isLikeHovered) {
            likeBtn = likeBtn | ftxui::bold | ftxui::bgcolor(theme.activeRow);
        }
        likeBtn = likeBtn | ftxui::reflect(m_queueLikeBoxes[rowIdx]);

        ftxui::Element addBtn = ftxui::text(" + ")
            | ftxui::color(theme.accentCyan);
        if (isAddHovered) {
            addBtn = addBtn | ftxui::bold | ftxui::bgcolor(theme.activeRow);
        }
        addBtn = addBtn | ftxui::reflect(m_queueAddBoxes[rowIdx]);

        ftxui::Element dlBtn = ftxui::text(" [↓] ") | ftxui::color(theme.accentCyan);
        if (isDlHovered) {
            dlBtn = dlBtn | ftxui::bold | ftxui::bgcolor(theme.activeRow);
        }
        dlBtn = dlBtn | ftxui::reflect(m_queueDlBoxes[rowIdx]);

        ftxui::Element rowElem = ftxui::hbox({
            ftxui::hbox(std::move(rowLeft)),
            std::move(metaElem) | ftxui::flex,
            ftxui::filler(),
            ftxui::text(FormatTime(t.duration)) | ftxui::color(theme.textMuted),
            ftxui::text(" "),
            likeBtn,
            ftxui::text(" "),
            addBtn,
            dlBtn,
            ftxui::text(" ")
        }) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 2);

        if (isCurrent) {
            rowElem = rowElem | ftxui::bgcolor(theme.activeRow);
        } else if (isSelected || isHovered) {
            rowElem = rowElem | ftxui::bgcolor(theme.highlight);
        }

        rowElem = rowElem | ftxui::reflect(m_queueRowBoxes[rowIdx]);
        rows.push_back(std::move(rowElem));
    }

    rows.push_back(ftxui::filler());

    return ftxui::vbox(std::move(rows))
        | ftxui::flex
        | ftxui::reflect(m_queueListBox);
}

ftxui::Element NowPlayingScreen::Render() {
    auto theme = m_theme;

    return ftxui::vbox({
        ftxui::text(""),
        RenderTopBlock(),
        ftxui::text(""),
        RenderControls(),
        ftxui::text(""),
        ftxui::separatorLight() | ftxui::color(theme.border),
        RenderQueue() | ftxui::flex,
        ftxui::text("")
    }) | ftxui::flex;
}

bool NowPlayingScreen::OnEvent(ftxui::Event event) {
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_mouseX = mouse.x;
        m_mouseY = mouse.y;

        // 0. Login Button in Unauthorized Card
        if (!m_isSourceAuthorized && m_loginBtnBox.Contain(mouse.x, mouse.y)) {
            m_isLoginBtnHovered = true;
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnLoginRequested) OnLoginRequested(m_activeSource);
                return true;
            }
        } else {
            m_isLoginBtnHovered = false;
        }

        // 1. Mouse click on Seek bar
        if (m_seekBarBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                int barWidth = m_seekBarBox.x_max - m_seekBarBox.x_min;
                if (barWidth > 0 && OnSeekRequested) {
                    float frac = static_cast<float>(mouse.x - m_seekBarBox.x_min) / static_cast<float>(barWidth);
                    frac = std::clamp(frac, 0.0f, 1.0f);
                    double totalSec = m_audio.GetLengthSeconds();
                    if (totalSec <= 0.0) totalSec = static_cast<double>(m_playlist.GetCurrentTrack().duration);
                    OnSeekRequested(frac * totalSec);
                    return true;
                }
            }
        }

        // 2. Play / Pause Button
        if (m_playPauseBtnBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnTogglePlayPauseRequested) OnTogglePlayPauseRequested();
                return true;
            }
        }

        // 3. Prev Button
        if (m_prevBtnBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnPrevTrackRequested) OnPrevTrackRequested();
                return true;
            }
        }

        // 4. Next Button
        if (m_nextBtnBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnNextTrackRequested) OnNextTrackRequested();
                return true;
            }
        }

        // 5. Shuffle Button
        if (m_shuffleBtnBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                m_queueCursor = 0;
                m_scrollOffset = 0;
                if (OnToggleShuffleRequested) OnToggleShuffleRequested();
                return true;
            }
        }

        // 6. Repeat Button
        if (m_repeatBtnBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnCycleRepeatRequested) OnCycleRepeatRequested();
                return true;
            }
        }

        // 7. Volume Minus / Plus
        if (m_volMinusBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                float v = std::clamp(m_audio.GetVolume() - 0.05f, 0.0f, 1.0f);
                if (OnVolumeChanged) OnVolumeChanged(v);
                return true;
            }
        }
        if (m_volPlusBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                float v = std::clamp(m_audio.GetVolume() + 0.05f, 0.0f, 1.0f);
                if (OnVolumeChanged) OnVolumeChanged(v);
                return true;
            }
        }

        // 7b. Equalizer Button
        if (m_eqBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnOpenEqualizerRequested) OnOpenEqualizerRequested();
                return true;
            }
        }

        // 8. Queue Action Buttons (Like, Add to playlist, Download) hover & click
        m_hoveredLikeRow = -1;
        m_hoveredAddRow = -1;
        m_hoveredDlRow = -1;

        for (size_t r = 0; r < m_queueLikeBoxes.size(); ++r) {
            if (m_queueLikeBoxes[r].Contain(mouse.x, mouse.y)) {
                m_hoveredLikeRow = static_cast<int>(r);
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleTrackIndices.size()) {
                        int trackIdx = m_visibleTrackIndices[r];
                        auto slice = m_playlist.GetQueueSlice(trackIdx, 1);
                        if (!slice.empty() && OnToggleLike) {
                            OnToggleLike(slice[0]);
                        }
                        return true;
                    }
                }
            }
        }

        for (size_t r = 0; r < m_queueAddBoxes.size(); ++r) {
            if (m_queueAddBoxes[r].Contain(mouse.x, mouse.y)) {
                m_hoveredAddRow = static_cast<int>(r);
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleTrackIndices.size()) {
                        int trackIdx = m_visibleTrackIndices[r];
                        auto slice = m_playlist.GetQueueSlice(trackIdx, 1);
                        if (!slice.empty() && OnAddToPlaylist) {
                            OnAddToPlaylist(slice[0]);
                        }
                        return true;
                    }
                }
            }
        }

        for (size_t r = 0; r < m_queueDlBoxes.size(); ++r) {
            if (m_queueDlBoxes[r].Contain(mouse.x, mouse.y)) {
                m_hoveredDlRow = static_cast<int>(r);
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleTrackIndices.size()) {
                        int trackIdx = m_visibleTrackIndices[r];
                        auto slice = m_playlist.GetQueueSlice(trackIdx, 1);
                        if (!slice.empty() && OnDownloadTrackRequested) {
                            OnDownloadTrackRequested(slice[0]);
                        }
                        return true;
                    }
                }
            }
        }

        // 9. Queue List hover & mouse clicks
        m_hoveredQueueRow = -1;
        if (m_hoveredLikeRow == -1 && m_hoveredAddRow == -1 && m_hoveredDlRow == -1) {
            for (size_t r = 0; r < m_queueRowBoxes.size(); ++r) {
                if (m_queueRowBoxes[r].Contain(mouse.x, mouse.y)) {
                    m_hoveredQueueRow = static_cast<int>(r);
                    if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                        if (r < m_visibleTrackIndices.size()) {
                            int trackIdx = m_visibleTrackIndices[r];
                            m_queueCursor = trackIdx;
                            if (OnPlayTrackRequested) {
                                OnPlayTrackRequested(trackIdx);
                            }
                            return true;
                        }
                    }
                }
            }
        }

        // 10. Queue List mouse wheel scrolling (scrolls the view without moving cursor!)
        if (m_queueListBox.Contain(mouse.x, mouse.y)) {
            int total = static_cast<int>(m_playlist.GetQueueSize());
            int maxScroll = std::max(0, total - m_visibleQueueRows);
            if (mouse.button == ftxui::Mouse::WheelUp) {
                m_scrollOffset = std::max(0, m_scrollOffset - 2);
                return true;
            } else if (mouse.button == ftxui::Mouse::WheelDown) {
                m_scrollOffset = std::min(maxScroll, m_scrollOffset + 2);
                return true;
            }
        }

        // 11. Visualizer mouse click -> cycle mode
        if (m_visBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                CycleVisualizerMode();
                return true;
            }
        }
    }

    // Keyboard events
    if (event == ftxui::Event::Character('v') || event == ftxui::Event::Character('V') ||
        event.character() == "м" || event.character() == "М") {
        CycleVisualizerMode();
        return true;
    } else if (event == ftxui::Event::Character('j') || event == ftxui::Event::Character('J') ||
               event.character() == "о" || event.character() == "О" || event == ftxui::Event::ArrowDown) {
        int total = static_cast<int>(m_playlist.GetQueueSize());
        if (m_queueCursor < total - 1) {
            m_queueCursor++;
            if (m_queueCursor >= m_scrollOffset + m_visibleQueueRows) {
                m_scrollOffset = m_queueCursor - m_visibleQueueRows + 1;
            }
            return true;
        }
    } else if (event == ftxui::Event::Character('k') || event == ftxui::Event::Character('K') ||
               event.character() == "л" || event.character() == "Л" || event == ftxui::Event::ArrowUp) {
        if (m_queueCursor > 0) {
            m_queueCursor--;
            if (m_queueCursor < m_scrollOffset) {
                m_scrollOffset = m_queueCursor;
            }
            return true;
        }
    } else if (event == ftxui::Event::Character('a') || event == ftxui::Event::Character('A') ||
               event.character() == "ф" || event.character() == "Ф") {
        int total = static_cast<int>(m_playlist.GetQueueSize());
        if (m_queueCursor >= 0 && m_queueCursor < total) {
            auto slice = m_playlist.GetQueueSlice(m_queueCursor, 1);
            if (!slice.empty() && OnAddToPlaylist) {
                OnAddToPlaylist(slice[0]);
                return true;
            }
        }
    } else if (event == ftxui::Event::Character('l') || event == ftxui::Event::Character('L') ||
               event.character() == "д" || event.character() == "Д") {
        int total = static_cast<int>(m_playlist.GetQueueSize());
        if (m_queueCursor >= 0 && m_queueCursor < total) {
            auto slice = m_playlist.GetQueueSlice(m_queueCursor, 1);
            if (!slice.empty() && OnToggleLike) {
                OnToggleLike(slice[0]);
                return true;
            }
        }
    } else if (event == ftxui::Event::Character('d') || event == ftxui::Event::Character('D') ||
               event.character() == "в" || event.character() == "В") {
        int total = static_cast<int>(m_playlist.GetQueueSize());
        if (m_queueCursor >= 0 && m_queueCursor < total) {
            auto slice = m_playlist.GetQueueSlice(m_queueCursor, 1);
            if (!slice.empty() && OnDownloadTrackRequested) {
                OnDownloadTrackRequested(slice[0]);
                return true;
            }
        }
    } else if (event == ftxui::Event::Return) {
        if (!m_isSourceAuthorized && m_activeSource.rfind("Custom:", 0) != 0 && m_activeSource != "Offline" && m_activeSource != "All") {
            if (OnLoginRequested) {
                OnLoginRequested(m_activeSource);
                return true;
            }
        }
        if (OnPlayTrackRequested) {
            OnPlayTrackRequested(m_queueCursor);
            return true;
        }
    }

    return false;
}

} // namespace tui
