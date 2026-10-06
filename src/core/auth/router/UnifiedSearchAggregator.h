#pragma once

#include <string>
#include <vector>
#include <functional>
#include "models/Track.h"

class IAudioProvider;

class UnifiedSearchAggregator {
public:
    using ProviderResolver = std::function<IAudioProvider*(const std::string&)>;

    explicit UnifiedSearchAggregator(ProviderResolver resolver);

    void Search(const std::string& source, const std::string& query, int count, int offset,
                std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback);

    void AddTrackToFavorites(const Track& track,
                             std::function<void(bool success, const std::string& error)> callback);

    void RemoveTrackFromFavorites(const Track& track,
                                std::function<void(bool success, const std::string& error)> callback);

private:
    ProviderResolver m_resolver;
};
