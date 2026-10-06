#include "TuiStringUtils.h"
#include <ftxui/dom/elements.hpp>
#include <cstdio>
#include <vector>

namespace tui::utils {

std::string PluralizeTracks(int count) {
    int rem10 = count % 10;
    int rem100 = count % 100;
    if (rem100 >= 11 && rem100 <= 19) return "треков";
    if (rem10 == 1) return "трек";
    if (rem10 >= 2 && rem10 <= 4) return "трека";
    return "треков";
}

std::string FormatDuration(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int totalSec = static_cast<int>(seconds);
    int m = totalSec / 60;
    int s = totalSec % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

std::string ScrollText(const std::string& text, int maxCols, int tick) {
    if (text.empty() || maxCols <= 0) return "";
    auto glyphs = ftxui::Utf8ToGlyphs(text);
    if (static_cast<int>(glyphs.size()) <= maxCols) {
        return text;
    }
    const int kGap = 4;
    int totalLen = static_cast<int>(glyphs.size()) + kGap;
    int startIdx = (tick > 0) ? (tick % totalLen) : 0;
    std::string result;
    result.reserve(maxCols * 4);
    for (int i = 0; i < maxCols; ++i) {
        int idx = (startIdx + i) % totalLen;
        if (idx < static_cast<int>(glyphs.size())) {
            result += glyphs[idx];
        } else {
            result += ' ';
        }
    }
    return result;
}

} // namespace tui::utils
