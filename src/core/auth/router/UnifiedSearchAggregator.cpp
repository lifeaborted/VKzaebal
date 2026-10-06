#include "UnifiedSearchAggregator.h"
#include "core/api/IAudioProvider.h"
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <memory>

UnifiedSearchAggregator::UnifiedSearchAggregator(ProviderResolver resolver)
    : m_resolver(std::move(resolver)) {}

void UnifiedSearchAggregator::Search(const std::string& source, const std::string& query, int count, int offset,
                                    std::function<void(const std::vector<Track>& tracks, const std::string& error)> callback) {
    if (query.empty()) {
        if (callback) callback({}, "");
        return;
    }

    std::string s = source;
    std::string lowerS = s;
    for (char& ch : lowerS) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    bool isAll = (lowerS == "all" || lowerS == "все" || lowerS.empty() ||
                  lowerS == "offline" || s.rfind("Custom:", 0) == 0 ||
                  (lowerS != "vk" && lowerS != "yandex" && lowerS != "youtube" && lowerS != "soundcloud" && lowerS != "spotify"));

    if (isAll) {
        std::vector<std::string> activeSources = {"VK", "Yandex", "YouTube", "SoundCloud"};
        const size_t numSources = activeSources.size();
        auto resultsPerSource = std::make_shared<std::vector<std::vector<Track>>>(numSources);
        auto remaining = std::make_shared<int>(static_cast<int>(numSources));

        for (size_t srcIdx = 0; srcIdx < numSources; ++srcIdx) {
            const auto& src = activeSources[srcIdx];
            IAudioProvider* prov = m_resolver ? m_resolver(src) : nullptr;
            if (!prov) {
                (*remaining)--;
                if (*remaining == 0 && callback) {
                    callback({}, "");
                }
                continue;
            }

            prov->SearchAudio(query, count, offset, [resultsPerSource, remaining, callback, srcIdx, numSources](const std::vector<Track>& res, const std::string&) {
                if (!res.empty()) {
                    (*resultsPerSource)[srcIdx] = res;
                }
                (*remaining)--;
                if (*remaining == 0) {
                    // Interleave results round-robin from each source for balanced relevance
                    size_t maxLen = 0;
                    for (const auto& list : *resultsPerSource) {
                        if (list.size() > maxLen) maxLen = list.size();
                    }
                    std::vector<Track> merged;
                    std::unordered_set<std::string> seenIds;
                    for (size_t i = 0; i < maxLen; ++i) {
                        for (size_t sIdx = 0; sIdx < numSources; ++sIdx) {
                            if (i < (*resultsPerSource)[sIdx].size()) {
                                const auto& tr = (*resultsPerSource)[sIdx][i];
                                if (!tr.id.empty()) {
                                    if (seenIds.insert(tr.id).second) {
                                        merged.push_back(tr);
                                    }
                                } else {
                                    merged.push_back(tr);
                                }
                            }
                        }
                    }
                    if (callback) callback(merged, "");
                }
            });
        }
    } else {
        IAudioProvider* prov = m_resolver ? m_resolver(s) : nullptr;
        if (!prov) {
            if (callback) callback({}, "Unknown provider: " + s);
            return;
        }
        prov->SearchAudio(query, count, offset, callback);
    }
}

void UnifiedSearchAggregator::AddTrackToFavorites(const Track& track, std::function<void(bool success, const std::string& error)> callback) {
    IAudioProvider* prov = m_resolver ? m_resolver(track.source) : nullptr;
    if (!prov) {
        if (callback) callback(false, "Unknown provider for track: " + track.source);
        return;
    }
    prov->AddTrackToFavorites(track.id, track.ownerId, callback);
}

void UnifiedSearchAggregator::RemoveTrackFromFavorites(const Track& track, std::function<void(bool success, const std::string& error)> callback) {
    IAudioProvider* prov = m_resolver ? m_resolver(track.source) : nullptr;
    if (!prov) {
        if (callback) callback(false, "Unknown provider for track: " + track.source);
        return;
    }
    prov->RemoveTrackFromFavorites(track.id, track.ownerId, callback);
}
