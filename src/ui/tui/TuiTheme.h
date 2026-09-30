#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace tui {

struct RGB {
    int r = 0;
    int g = 0;
    int b = 0;
};

// --- Theme Colors ---
struct ThemePalette {
    std::string name;
    ftxui::Color bg;
    ftxui::Color panelBg;
    ftxui::Color cardBg;
    ftxui::Color border;
    ftxui::Color borderFocus;
    ftxui::Color text;
    ftxui::Color textMuted;
    ftxui::Color accent;       // primary accent (green / cyan)
    ftxui::Color accentCyan;
    ftxui::Color accentOrange;
    ftxui::Color accentPurple;
    ftxui::Color accentRed;
    ftxui::Color highlight;
    ftxui::Color activeRow;

    // Progress bar colors
    ftxui::Color progressPlayed;
    ftxui::Color progressThumb;
    ftxui::Color progressRemaining;

    std::vector<RGB> visualizerGradient;
};

inline ThemePalette GetDefaultTheme() {
    ThemePalette p;
    p.name = "Neon Cyberpunk";
    p.bg = ftxui::Color::RGB(13, 17, 23);
    p.panelBg = ftxui::Color::RGB(22, 27, 34);
    p.cardBg = ftxui::Color::RGB(27, 33, 44);
    p.border = ftxui::Color::RGB(48, 54, 61);
    p.borderFocus = ftxui::Color::RGB(88, 166, 255);
    p.text = ftxui::Color::RGB(201, 209, 217);
    p.textMuted = ftxui::Color::RGB(139, 148, 158);
    p.accent = ftxui::Color::RGB(80, 255, 150);      // vibrant emerald
    p.accentCyan = ftxui::Color::RGB(88, 166, 255);  // cyan blue
    p.accentOrange = ftxui::Color::RGB(255, 184, 108); // warm amber
    p.accentPurple = ftxui::Color::RGB(188, 140, 255); // neon purple
    p.accentRed = ftxui::Color::RGB(255, 123, 114);   // coral red
    p.highlight = ftxui::Color::RGB(33, 38, 45);
    p.activeRow = ftxui::Color::RGB(31, 53, 41);

    p.progressPlayed = ftxui::Color::RGB(80, 255, 150);
    p.progressThumb = ftxui::Color::RGB(255, 255, 255);
    p.progressRemaining = ftxui::Color::RGB(70, 70, 70);

    p.visualizerGradient = {
        {80, 255, 150},   // Green at low peaks
        {255, 184, 108},  // Amber at mid peaks
        {255, 80, 80}     // Red at high peaks
    };
    return p;
}

inline ftxui::Color InterpolateColor(float t, const std::vector<RGB>& stops) {
    if (stops.empty()) return ftxui::Color::White;
    if (stops.size() == 1 || t <= 0.0f) {
        return ftxui::Color::RGB(stops[0].r, stops[0].g, stops[0].b);
    }
    if (t >= 1.0f) {
        const auto& last = stops.back();
        return ftxui::Color::RGB(last.r, last.g, last.b);
    }

    float scaled = t * static_cast<float>(stops.size() - 1);
    int idx = static_cast<int>(scaled);
    if (idx >= static_cast<int>(stops.size()) - 1) idx = static_cast<int>(stops.size()) - 2;
    if (idx < 0) idx = 0;
    float frac = scaled - static_cast<float>(idx);

    const auto& c1 = stops[idx];
    const auto& c2 = stops[idx + 1];

    int r = static_cast<int>(c1.r + (c2.r - c1.r) * frac);
    int g = static_cast<int>(c1.g + (c2.g - c1.g) * frac);
    int b = static_cast<int>(c1.b + (c2.b - c1.b) * frac);

    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    return ftxui::Color::RGB(r, g, b);
}

// Format source name
inline std::string FormatSourceShort(const std::string& source) {
    std::string s = source;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "vk" || s == "vkontakte") return "VK";
    if (s == "ya" || s == "yandex")   return "YA";
    if (s == "sc" || s == "soundcloud") return "SC";
    if (s == "yt" || s == "youtube")   return "YT";
    if (s == "sp" || s == "spotify")   return "SP";
    if (s == "off" || s == "offline") return "OFF";
    if (s == "all") return "ALL";
    if (s.size() > 2) return s.substr(0, 2);
    return source;
}

// Service badge color resolver
inline ftxui::Color GetServiceColor(const std::string& source) {
    std::string s = source;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "vk" || s == "vkontakte") return ftxui::Color::RGB(70, 128, 255);   // VK Blue
    if (s == "yandex" || s == "ya")    return ftxui::Color::RGB(255, 204, 0);    // Yandex Yellow
    if (s == "soundcloud" || s == "sc") return ftxui::Color::RGB(255, 102, 0);  // SoundCloud Orange
    if (s == "youtube" || s == "yt")   return ftxui::Color::RGB(255, 51, 51);   // YouTube Red
    if (s == "spotify" || s == "sp")   return ftxui::Color::RGB(30, 215, 96);   // Spotify Green
    if (s == "offline" || s == "off") return ftxui::Color::RGB(150, 150, 150);
    if (s == "all")                    return ftxui::Color::RGB(80, 255, 150);
    return ftxui::Color::RGB(150, 150, 150);
}

inline std::string GetServiceIcon(const std::string& source) {
    return "[" + FormatSourceShort(source) + "]";
}

} // namespace tui
