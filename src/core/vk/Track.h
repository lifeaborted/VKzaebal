#pragma once
#include <string>

struct Track {
    long long id;
    long long ownerId;
    std::string artist;
    std::string title;
    std::string url;
    int duration; // в секундах

    // вывода времени (ММ:СС)
    std::string GetFormattedDuration() const {
        std::string min = std::to_string(duration / 60);
        std::string sec = std::to_string(duration % 60);

        // Добавляем ведущий ноль для секунд
        if (sec.length() < 2) {
            sec = "0" + sec;
        }

        return min + ":" + sec;
    }
};