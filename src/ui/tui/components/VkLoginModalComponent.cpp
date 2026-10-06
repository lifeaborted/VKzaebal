#include "VkLoginModalComponent.h"
#include <ftxui/dom/elements.hpp>

namespace tui {

VkLoginModalComponent::VkLoginModalComponent() {
    using namespace ftxui;

    InputOption loginOpt;
    loginOpt.multiline = false;
    m_loginInput = Input(&m_login, "+7... или email", loginOpt);

    InputOption passOpt;
    passOpt.multiline = false;
    passOpt.password = true;
    m_passwordInput = Input(&m_password, "Пароль", passOpt);

    InputOption codeOpt;
    codeOpt.multiline = false;
    m_codeInput = Input(&m_code, "6-значный код", codeOpt);

    auto submitBtn = Button("  Войти  ", [this]() {
        if (!m_login.empty()) {
            m_currentStep = Step::LOADING;
            m_statusMsg = "Авторизация в VK...";
            m_errorMsg.clear();
            if (OnSubmitCredentials) OnSubmitCredentials(m_login, m_password);
        } else {
            m_errorMsg = "Введите логин (телефон или email)";
        }
    });

    auto cancelBtn = Button(" Отмена ", [this]() {
        Hide();
    });

    auto credButtons = Container::Horizontal({submitBtn, cancelBtn});
    m_credentialsContainer = Container::Vertical({
        m_loginInput,
        m_passwordInput,
        credButtons
    });

    auto submitCodeBtn = Button(" Подтвердить ", [this]() {
        if (!m_code.empty()) {
            m_currentStep = Step::LOADING;
            m_statusMsg = "Проверка кода...";
            m_errorMsg.clear();
            if (OnSubmitCode) OnSubmitCode(m_code);
        } else {
            m_errorMsg = "Введите код подтверждения";
        }
    });

    auto backBtn = Button(" Назад ", [this]() {
        m_currentStep = Step::CREDENTIALS;
        m_code.clear();
        m_errorMsg.clear();
    });

    auto codeButtons = Container::Horizontal({submitCodeBtn, backBtn});
    m_codeContainer = Container::Vertical({
        m_codeInput,
        codeButtons
    });

    // Dummy empty container for loading step
    auto loadingContainer = Container::Vertical({});

    m_mainContainer = Container::Tab({
        m_credentialsContainer,
        m_codeContainer,
        loadingContainer
    }, reinterpret_cast<int*>(&m_currentStep));

    Add(m_mainContainer);
}

void VkLoginModalComponent::UpdateTheme(const ThemePalette& palette) {
    m_theme = palette;
}

void VkLoginModalComponent::Show() {
    m_isVisible = true;
    m_currentStep = Step::CREDENTIALS;
    m_errorMsg.clear();
    m_statusMsg.clear();
    m_code.clear();
}

void VkLoginModalComponent::Hide() {
    m_isVisible = false;
    ResetFields();
    if (OnCloseRequested) {
        OnCloseRequested();
    }
}

bool VkLoginModalComponent::IsTyping() const {
    return m_isVisible && (m_currentStep == Step::CREDENTIALS || m_currentStep == Step::CODE_2FA);
}

void VkLoginModalComponent::ResetFields() {
    m_password.clear();
    m_code.clear();
    m_errorMsg.clear();
    m_statusMsg.clear();
    m_phoneMask.clear();
    m_verificationMethod.clear();
    m_currentStep = Step::CREDENTIALS;
}

void VkLoginModalComponent::SetCodeRequired(const std::string& method, const std::string& phoneMask) {
    m_verificationMethod = method;
    m_phoneMask = phoneMask;
    m_currentStep = Step::CODE_2FA;
    m_errorMsg.clear();
    m_statusMsg.clear();
}

void VkLoginModalComponent::SetStatus(const std::string& status) {
    m_statusMsg = status;
}

void VkLoginModalComponent::SetError(const std::string& error) {
    m_errorMsg = error;
    if (m_currentStep == Step::LOADING) {
        m_currentStep = Step::CREDENTIALS;
    }
}

void VkLoginModalComponent::SetLoading(bool loading) {
    if (loading) {
        m_currentStep = Step::LOADING;
    } else if (m_currentStep == Step::LOADING) {
        m_currentStep = Step::CREDENTIALS;
    }
}

ftxui::Element VkLoginModalComponent::RenderCredentialsStep() {
    using namespace ftxui;

    Elements form;
    form.push_back(text(" Логин (телефон или e-mail):") | color(m_theme.textMuted));
    form.push_back(m_loginInput->Render() | borderRounded | color(m_theme.borderFocus));
    form.push_back(separatorEmpty());

    form.push_back(text(" Пароль:") | color(m_theme.textMuted));
    form.push_back(m_passwordInput->Render() | borderRounded | color(m_theme.borderFocus));
    form.push_back(separatorEmpty());

    if (!m_errorMsg.empty()) {
        form.push_back(text(" ✗ " + m_errorMsg) | color(m_theme.accentRed) | bold | center);
        form.push_back(separatorEmpty());
    } else if (!m_statusMsg.empty()) {
        form.push_back(text(" ℹ " + m_statusMsg) | color(m_theme.accentOrange) | center);
        form.push_back(separatorEmpty());
    }

    form.push_back(m_credentialsContainer->ChildAt(2)->Render() | center);

    return vbox(std::move(form));
}

ftxui::Element VkLoginModalComponent::RenderCodeStep() {
    using namespace ftxui;

    Elements form;
    std::string prompt = "Введите код подтверждения";
    if (!m_phoneMask.empty()) {
        prompt += " (" + m_phoneMask + ")";
    }
    if (!m_verificationMethod.empty()) {
        prompt += " [" + m_verificationMethod + "]";
    }

    form.push_back(text(" " + prompt) | color(m_theme.accentCyan) | center);
    form.push_back(separatorEmpty());

    form.push_back(m_codeInput->Render() | borderRounded | color(m_theme.borderFocus) | size(WIDTH, EQUAL, 24) | center);
    form.push_back(separatorEmpty());

    if (!m_errorMsg.empty()) {
        form.push_back(text(" ✗ " + m_errorMsg) | color(m_theme.accentRed) | bold | center);
        form.push_back(separatorEmpty());
    }

    form.push_back(m_codeContainer->ChildAt(1)->Render() | center);

    return vbox(std::move(form));
}

ftxui::Element VkLoginModalComponent::RenderLoadingStep() {
    using namespace ftxui;

    std::string msg = m_statusMsg.empty() ? "Авторизация в VK..." : m_statusMsg;

    return vbox({
        separatorEmpty(),
        text(" ● " + msg) | color(m_theme.accent) | bold | center,
        separatorEmpty(),
        text("Пожалуйста, подождите...") | color(m_theme.textMuted) | center,
        separatorEmpty()
    });
}

ftxui::Element VkLoginModalComponent::Render() {
    using namespace ftxui;

    if (!m_isVisible) return emptyElement();

    auto titleElem = text(" ВХОД В VKONTAKTE ") | bold | color(m_theme.accent) | center;

    Element stepContent;
    if (m_currentStep == Step::CREDENTIALS) {
        stepContent = RenderCredentialsStep();
    } else if (m_currentStep == Step::CODE_2FA) {
        stepContent = RenderCodeStep();
    } else {
        stepContent = RenderLoadingStep();
    }

    auto footerElem = text(" [Tab] Переход   [Enter] Выбрать   [Esc] Отмена ") | color(m_theme.textMuted) | center;

    auto modalBox = vbox({
        titleElem,
        separator(),
        separatorEmpty(),
        stepContent,
        separatorEmpty(),
        separator(),
        footerElem
    }) | borderRounded | color(m_theme.border) | bgcolor(m_theme.panelBg) | clear_under | size(WIDTH, GREATER_THAN, 56) | center;

    return dbox({
        modalBox
    });
}

bool VkLoginModalComponent::OnEvent(ftxui::Event event) {
    if (!m_isVisible) return false;

    if (event == ftxui::Event::Escape) {
        Hide();
        return true;
    }

    // Enter submit shortcuts
    if (event == ftxui::Event::Return) {
        if (m_currentStep == Step::CREDENTIALS) {
            if (m_loginInput->Focused()) {
                m_passwordInput->TakeFocus();
                return true;
            } else if (m_passwordInput->Focused()) {
                if (!m_login.empty()) {
                    m_currentStep = Step::LOADING;
                    m_statusMsg = "Авторизация в VK...";
                    m_errorMsg.clear();
                    if (OnSubmitCredentials) OnSubmitCredentials(m_login, m_password);
                    return true;
                }
            }
        } else if (m_currentStep == Step::CODE_2FA) {
            if (m_codeInput->Focused() && !m_code.empty()) {
                m_currentStep = Step::LOADING;
                m_statusMsg = "Проверка кода...";
                m_errorMsg.clear();
                if (OnSubmitCode) OnSubmitCode(m_code);
                return true;
            }
        }
    }

    return m_mainContainer->OnEvent(event);
}

} // namespace tui
