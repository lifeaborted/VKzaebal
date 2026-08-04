#pragma once
#include <string>

struct Track {
    std::string id;
    std::string ownerId;
    std::string artist;
    std::string title;
    std::string url;
    int duration = 0; // в секундах
    std::string coverUrl = "";

    // Вывод времени (ММ:СС)
    std::string GetFormattedDuration() const {
        int m = duration / 60;
        int s = duration % 60;
        std::string min = std::to_string(m);
        std::string sec = s < 10 ? "0" + std::to_string(s) : std::to_string(s);
        return min + ":" + sec;
    }
};