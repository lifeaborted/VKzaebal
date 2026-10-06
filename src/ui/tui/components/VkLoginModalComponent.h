#pragma once

#include "ui/tui/TuiTheme.h"
#include "ui/tui/modals/IModalDialog.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <functional>

namespace tui {

class VkLoginModalComponent : public IModalDialog {
public:
    enum class Step {
        CREDENTIALS,
        CODE_2FA,
        LOADING
    };

    VkLoginModalComponent();
    ~VkLoginModalComponent() override = default;

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void UpdateTheme(const ThemePalette& palette) override;
    void Show();
    void Hide() override;
    bool IsVisible() const override { return m_isVisible; }
    bool IsTyping() const override;

    // External state setters (called via signals from VkAuthService)
    void SetCodeRequired(const std::string& method, const std::string& phoneMask);
    void SetStatus(const std::string& status);
    void SetError(const std::string& error);
    void SetLoading(bool loading);
    void ResetFields();

    // Callbacks to external controller
    std::function<void(const std::string& login, const std::string& password)> OnSubmitCredentials;
    std::function<void(const std::string& code)> OnSubmitCode;
    std::function<void()> OnCloseRequested;

private:
    ftxui::Element RenderCredentialsStep();
    ftxui::Element RenderCodeStep();
    ftxui::Element RenderLoadingStep();

    bool m_isVisible = false;
    ThemePalette m_theme;
    Step m_currentStep = Step::CREDENTIALS;

    std::string m_login;
    std::string m_password;
    std::string m_code;
    std::string m_statusMsg;
    std::string m_errorMsg;
    std::string m_phoneMask;
    std::string m_verificationMethod;

    ftxui::Component m_loginInput;
    ftxui::Component m_passwordInput;
    ftxui::Component m_codeInput;

    ftxui::Component m_credentialsContainer;
    ftxui::Component m_codeContainer;
    ftxui::Component m_mainContainer;
};

} // namespace tui
