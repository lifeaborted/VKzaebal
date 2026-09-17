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
    m_ultimateRenderer = std::make_unique<UltimateRenderer>(audio, playlist);
}

ConsoleRenderer::~ConsoleRenderer() = default;

void ConsoleRenderer::SetVisualizerEnabled(bool enabled) {
    m_showVisualizer = enabled;
}

void ConsoleRenderer::Render() {
    m_ultimateRenderer->Render();
}

void ConsoleRenderer::ReloadConfig() {
    m_ultimateRenderer->ReloadConfig();
}

int ConsoleRenderer::GetFramerate() const {
    return m_ultimateRenderer->GetFramerate();
}

void ConsoleRenderer::RequestFullRedraw() {
    m_ultimateRenderer->RequestFullRedraw();
}

void ConsoleRenderer::SetStatusMessage(const std::string& msg) {
    m_ultimateRenderer->SetStatusMessage(msg);
}

void ConsoleRenderer::SetOverlay(const std::string& msg) {
    m_ultimateRenderer->SetOverlay(msg);
}