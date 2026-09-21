#pragma once

#include <string>
#include <QStringList>

class WebViewCookieReader {
public:
    static std::string GetFullYouTubeCookies();
    static std::string GetFullVkCookies();
    static std::string GetCookiesForDomains(const QStringList& domainPatterns);
    static bool ClearServiceCache(const std::string& service);
};
