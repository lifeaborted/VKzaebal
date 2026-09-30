#pragma once

#include "ui/tui/TuiTheme.h"
#include <QObject>
#include <QString>
#include <QFileSystemWatcher>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace tui {

enum class BackgroundMode {
    MONO = 0,
    GRADIENT = 1
};

struct BackgroundConfig {
    BackgroundMode mode = BackgroundMode::GRADIENT;
    ftxui::Color color = ftxui::Color::RGB(13, 17, 23);
    std::vector<RGB> gradientStops = {
        {35, 15, 45},
        {18, 12, 28},
        {10, 8, 18}
    };
};

struct VisualizerConfig {
    int mode = 0;              // 0 = CAVA Equalizer, 1 = Symmetrical Wave, 2 = Silk Wave
    int barWidth = 1;          // Width of each bar column (1 or 2)
    int barSpacing = 1;        // Space between bars (0 = solid, 1 = spaced)
    float smoothing = 0.72f;   // Falloff smoothing (0.1 - 0.98)
    float sensitivity = 1.25f; // Amplitude scaling multiplier
    bool showPeaks = true;     // Show floating peak caps
    ftxui::Color colorPeak = ftxui::Color::RGB(255, 123, 114);

    // Vertical Level Gradient for equalizer bars (bottom to top)
    std::vector<RGB> gradientStops = {
        {50, 255, 150},   // Bottom / low level (Green)
        {255, 184, 108},  // Mid level (Amber)
        {255, 80, 80}     // Top / peak level (Red)
    };
};

class TuiThemeConfig : public QObject {
    Q_OBJECT
public:
    explicit TuiThemeConfig(QObject* parent = nullptr);
    ~TuiThemeConfig() override = default;

    void Initialize();
    void Reload();

    ThemePalette GetTheme() const { return m_theme; }
    VisualizerConfig GetVisualizerConfig() const { return m_visConfig; }
    BackgroundConfig GetBackgroundConfig() const { return m_bgConfig; }
    ftxui::Color GetServiceColor(const std::string& source) const;

    QString GetConfigFilePath() const { return m_configPath; }

    // Color parsers supporting "R, G, B", "R G B", and "#RRGGBB"
    static ftxui::Color ParseColor(const std::string& str, const ftxui::Color& fallback);
    static RGB ParseRgbStruct(const std::string& str, const RGB& fallback = {0, 0, 0});
    static std::vector<RGB> ParseColorList(const std::string& str, const std::vector<RGB>& fallback);
    void SaveVisualizerConfig(const VisualizerConfig& cfg);
    void SaveBackgroundMode(BackgroundMode mode);
    void SaveAccentColor(const std::string& rgbStr);

    std::string GetRawValue(const std::string& section, const std::string& key, const std::string& fallback = "") const;
    void SetRawValue(const std::string& section, const std::string& key, const std::string& value);

    static void SaveCfgValue(const QString& filePath, const std::string& section, const std::string& key, const std::string& value);

    // Callback fired when ftxui.cfg is modified on disk
    std::function<void()> OnConfigChanged;

private slots:
    void OnConfigFileChanged(const QString& path);

private:
    void EnsureConfigFileExists();
    void CreateDefaultConfigFile(const QString& path);
    void ParseConfigFile(const QString& path);

    QString m_configPath;
    QFileSystemWatcher* m_watcher = nullptr;

    ThemePalette m_theme;
    BackgroundConfig m_bgConfig;
    VisualizerConfig m_visConfig;
    std::unordered_map<std::string, ftxui::Color> m_serviceColors;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_rawValues;
};

} // namespace tui
