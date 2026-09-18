#pragma once

#include <string>

class WebViewCookieReader {
public:
    static std::string GetFullYouTubeCookies();
    static bool ClearServiceCache(const std::string& service);
};
