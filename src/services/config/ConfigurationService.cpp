#include "ConfigurationService.h"
#include "utils/path/PathManager.h"

#include <QSettings>
#include <QFile>
#include <algorithm>

ConfigurationService::ConfigurationService() = default;

void ConfigurationService::EnsureDefaultConfig() {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    if (!settings.contains("Session/Volume")) {
        settings.setValue("Audio/CrossfadeDurationMs", 3000);
        settings.setValue("Audio/CrossfadePlayback", false);
        settings.setValue("Session/Volume", 1.0f);
        settings.setValue("Session/Shuffle", false);
        settings.setValue("Session/AutoPlay", false);
        settings.setValue("Session/Repeat", 1);
        settings.setValue("Playback/SavePosition", 2);
        settings.setValue("General/source", "VK");
        settings.setValue("Ui/ShowVisualizer", true);
        settings.setValue("Downloads/Path", "");
        settings.sync();
    } else {
        if (!settings.contains("Downloads/Path")) {
            settings.setValue("Downloads/Path", "");
        }
        if (!settings.contains("Playback/SavePosition")) {
            settings.setValue("Playback/SavePosition", 2);
        }
        if (settings.contains("Session/Position")) {
            settings.remove("Session/Position");
        }
        if (settings.contains("Session/CurrentTrackIndex")) {
            settings.remove("Session/CurrentTrackIndex");
        }
        settings.sync();
    }

    QString ultimatePath = PathManager::GetUltimateConfigPath();
    if (!QFile::exists(ultimatePath)) {
        QSettings defaultUltimate(ultimatePath, QSettings::IniFormat);
        defaultUltimate.setValue("Visualizer/Height", 10);
        defaultUltimate.setValue("Visualizer/Width", 0);
        defaultUltimate.setValue("Visualizer/BarWidth", 2);
        defaultUltimate.setValue("Visualizer/BarSpacing", 1);
        defaultUltimate.setValue("Visualizer/BlockSpacing", 1);
        defaultUltimate.setValue("Visualizer/DrawBorders", true);
        defaultUltimate.setValue("Visualizer/Layout", "Visualizer,ProgressBar,TrackInfo");
        defaultUltimate.setValue("Visualizer/PaddingLeft", 2);
        defaultUltimate.setValue("Visualizer/Color", "gradient");
        defaultUltimate.setValue("Visualizer/GradientColors", "#32FF96,#F0B432,#FF5050");
        defaultUltimate.setValue("Background/Enabled", false);
        defaultUltimate.setValue("Background/Color", "gradient");
        defaultUltimate.setValue("Background/GradientColors", "#1E1E1E,#000000");
        defaultUltimate.setValue("Visualizer/Framerate", 30);
        defaultUltimate.setValue("Visualizer/Smoothing", 0.5);
        defaultUltimate.sync();
    }
}

std::string ConfigurationService::GetActiveSource() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("General/source", "VK").toString().toStdString();
}

void ConfigurationService::SetActiveSource(const std::string& source) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("General/source", QString::fromStdString(source));
    settings.sync();
}

bool ConfigurationService::GetShuffle() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Session/Shuffle", false).toBool();
}

void ConfigurationService::SetShuffle(bool shuffle) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Session/Shuffle", shuffle);
    settings.sync();
}

int ConfigurationService::GetRepeatMode() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Session/Repeat", 1).toInt();
}

void ConfigurationService::SetRepeatMode(int mode) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Session/Repeat", mode);
    settings.sync();
}

float ConfigurationService::GetVolume() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Session/Volume", 1.0f).toFloat();
}

void ConfigurationService::SetVolume(float vol) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Session/Volume", vol);
    settings.sync();
}

bool ConfigurationService::GetAutoPlay() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Session/AutoPlay", false).toBool();
}

void ConfigurationService::SetAutoPlay(bool autoPlay) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Session/AutoPlay", autoPlay);
    settings.sync();
}

bool ConfigurationService::GetCrossfadeEnabled() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Audio/CrossfadePlayback", false).toBool();
}

void ConfigurationService::SetCrossfadeEnabled(bool enabled) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Audio/CrossfadePlayback", enabled);
    settings.sync();
}

int ConfigurationService::GetCrossfadeDurationMs() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Audio/CrossfadeDurationMs", 3000).toInt();
}

void ConfigurationService::SetCrossfadeDurationMs(int ms) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Audio/CrossfadeDurationMs", ms);
    settings.sync();
}

int ConfigurationService::GetSavePositionMode() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    QVariant val = settings.value("Playback/SavePosition", 2);
    if (val.typeId() == QMetaType::Bool) {
        return val.toBool() ? 2 : 1;
    }
    bool ok = false;
    int m = val.toInt(&ok);
    if (ok) return std::clamp(m, 0, 2);
    return 2;
}

void ConfigurationService::SetSavePositionMode(int mode) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Playback/SavePosition", mode);
    settings.sync();
}

bool ConfigurationService::GetShowVisualizer() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Ui/ShowVisualizer", true).toBool();
}

void ConfigurationService::SetShowVisualizer(bool show) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Ui/ShowVisualizer", show);
    settings.sync();
}

QString ConfigurationService::GetDownloadsPath() const {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    return settings.value("Downloads/Path", "").toString();
}

void ConfigurationService::SetDownloadsPath(const QString& path) {
    QSettings settings(PathManager::GetConfigPath(), QSettings::IniFormat);
    settings.setValue("Downloads/Path", path);
    settings.sync();
}
