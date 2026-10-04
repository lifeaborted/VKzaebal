#pragma once

#include <QObject>
#include <string>
#include <vector>
#include <functional>

struct Track;
class IAudioProvider;

enum class UiMode {
    TUI,        // FTXUI современный терминальный интерфейс
    CLI,        // UltimateRenderer консольный интерфейс
    CLI_LIGHT   // Минимальный легковесный консольный интерфейс (заготовка)
};

// ==============================================================================
// IUiController
// ------------------------------------------------------------------------------
// Абстрактный базовый интерфейс контроллера пользовательского интерфейса.
// Полностью изолирует ядро (ApplicationCore) от конкретных реализаций UI
// (FTXUI, UltimateRenderer, Light CLI, а в будущем — Desktop GUI и Android).
// ==============================================================================
class IUiController : public QObject {
    Q_OBJECT
public:
    explicit IUiController(QObject* parent = nullptr) : QObject(parent) {}
    ~IUiController() override = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void SetStatusMessage(const std::string& msg) = 0;

    virtual void SetCurrentProvider(IAudioProvider* provider) { Q_UNUSED(provider); }
    virtual void SetWaitingAuth(bool waiting) { Q_UNUSED(waiting); }
    virtual void OnTrackChanged(const Track& track) { Q_UNUSED(track); }
    virtual void OnAudioFetched(const std::vector<Track>& tracks) { Q_UNUSED(tracks); }
    virtual void OnFinishedFetching() {}

    std::function<void(bool)> OnGaplessModeChanged;

signals:
    void QuitRequested();
    void OfflineModeRequested();
    void SourceChanged(const std::string& sourceName);
    void LogoutRequested(const std::string& service);
};
