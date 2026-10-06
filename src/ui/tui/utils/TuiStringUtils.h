#pragma once

#include <string>

namespace tui::utils {

// Pluralizes Russian word for tracks: 1 трек, 2 трека, 5 треков
std::string PluralizeTracks(int count);

// Formats seconds into MM:SS
std::string FormatDuration(double seconds);
inline std::string FormatTime(double seconds) { return FormatDuration(seconds); }

// Scrolls text horizontally within maxCols with looping and gap
std::string ScrollText(const std::string& text, int maxCols, int tick);

} // namespace tui::utils
