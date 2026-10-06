#include "SearchScreen.h"
#include "ui/tui/utils/TuiStringUtils.h"
#include "ui/tui/TuiTheme.h"
#include "ui/tui/CoverArtRenderer.h"
#include "ui/tui/components/SidebarComponent.h"
#include "core/audio/IAudioEngine.h"
#include "core/playlist/PlaylistManager.h"
#include <ftxui/component/component.hpp>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#ifdef RGB
#undef RGB
#endif
#endif

namespace tui {

using utils::FormatTime;
using utils::ScrollText;

SearchScreen::SearchScreen(IAudioEngine& audio,
                           PlaylistManager& playlist,
                           CoverArtRenderer& coverRenderer,
                           std::shared_ptr<SidebarComponent> sidebar)
    : m_audio(audio),
      m_playlist(playlist),
      m_coverRenderer(coverRenderer),
      m_sidebar(std::move(sidebar)) {
    ftxui::InputOption opt;
    opt.multiline = false;
    m_inputComponent = ftxui::Input(&m_searchQuery, "Введите запрос для поиска треков (или нажмите /)...", opt);

    Add(m_inputComponent);
}

void SearchScreen::SetSearchResults(const std::vector<Track>& results, const std::string& error) {
    m_searchResults = results;
    m_errorMessage = error;
    m_isLoading = false;
    m_isLoadingMore = false;
    m_hasMoreResults = (results.size() >= 15);
    m_selectedResultIndex = 0;
    m_scrollOffset = 0;
    m_isBrowsingResults = !results.empty();
    m_isInputActive = false;
}

void SearchScreen::AppendSearchResults(const std::vector<Track>& moreResults) {
    m_isLoadingMore = false;
    if (moreResults.empty()) {
        m_hasMoreResults = false;
        return;
    }

    std::unordered_set<std::string> existing;
    for (const auto& t : m_searchResults) {
        if (!t.id.empty()) existing.insert(t.id);
    }

    for (const auto& t : moreResults) {
        if (t.id.empty() || existing.find(t.id) == existing.end()) {
            m_searchResults.push_back(t);
            if (!t.id.empty()) existing.insert(t.id);
        }
    }

    if (moreResults.size() < 10) {
        m_hasMoreResults = false;
    }
}

void SearchScreen::AddTrackToPlaylistMap(const std::string& trackId, const std::string& playlistName) {
    if (trackId.empty() || playlistName.empty()) return;
    auto it = m_trackPlaylistMap.find(trackId);
    if (it == m_trackPlaylistMap.end() || it->second.empty()) {
        m_trackPlaylistMap[trackId] = playlistName;
    } else if (it->second.find(playlistName) == std::string::npos) {
        it->second += ", " + playlistName;
    }
}

void SearchScreen::CheckTriggerLoadMore() {
    if (m_searchMode == SearchMode::API && !m_isOnline) {
        return;
    }
    if (m_searchMode == SearchMode::API && !m_isLoading && !m_isLoadingMore && m_hasMoreResults && !m_searchQuery.empty()) {
        if (m_selectedResultIndex + 6 >= static_cast<int>(m_searchResults.size())) {
            m_isLoadingMore = true;
            if (OnLoadMoreResults) {
                OnLoadMoreResults(m_searchQuery, m_searchSource, static_cast<int>(m_searchResults.size()));
            }
        }
    }
}

void SearchScreen::SetSearchLoading(bool loading) {
    m_isLoading = loading;
    if (loading) {
        m_errorMessage.clear();
        m_isInputActive = false;
        m_isLoadingMore = false;
    }
}

void SearchScreen::FocusSearchInput() {
    m_isInputActive = true;
    m_isBrowsingResults = false;
    if (m_inputComponent) {
        m_inputComponent->TakeFocus();
    }
}

void SearchScreen::ToggleSearchMode() {
    m_searchMode = (m_searchMode == SearchMode::LOCAL) ? SearchMode::API : SearchMode::LOCAL;
    if (OnSearchModeChanged) {
        OnSearchModeChanged(m_searchMode);
    }
    if (OnPerformSearch) {
        OnPerformSearch(m_searchQuery, m_searchSource, m_searchMode);
    }
}

void SearchScreen::SetSearchSource(const std::string& src) {
    m_searchSource = src.empty() ? "All" : src;
}

std::string SearchScreen::GetSearchQuery() const {
    return m_searchQuery;
}

ftxui::Element SearchScreen::RenderCenterColumn() {
    auto theme = m_theme;

    // 1. Search Input Field (Permanently highlighted with activeRow; switches to highlight on click)
    ftxui::Element inputElem;
    if (m_isInputActive) {
        inputElem = ftxui::hbox({
            ftxui::text(" Поиск: ")
                | ftxui::bold
                | ftxui::color(theme.accent),
            m_inputComponent->Render() | ftxui::flex,
            ftxui::text(" ")
        }) | ftxui::bgcolor(theme.highlight) | ftxui::reflect(m_inputBox);
    } else {
        auto inputContent = ftxui::hbox({
            ftxui::text(" Поиск: ")
                | ftxui::bold
                | ftxui::color(theme.accent),
            ftxui::text(m_searchQuery.empty() ? "Введите запрос для поиска треков (или нажмите /)..." : m_searchQuery)
                | ftxui::color(m_searchQuery.empty() ? theme.textMuted : theme.text)
                | ftxui::flex,
            ftxui::text(" ")
        }) | ftxui::bgcolor(theme.activeRow);
        inputElem = inputContent | ftxui::reflect(m_inputBox);
    }

    // 2. Stats and Search Source & Mode Row
    std::string statsText;
    if (m_isLoading) {
        statsText = (m_searchMode == SearchMode::LOCAL) ? "Поиск в базе..." : "Поиск в сети...";
    } else if (m_isLoadingMore) {
        statsText = "Найдено треков: " + std::to_string(m_searchResults.size()) + " [подгрузка...]";
    } else if (m_searchMode == SearchMode::LOCAL) {
        statsText = "Найдено треков: " + std::to_string(m_searchResults.size());
    } else {
        statsText = "";
    }

    std::string modeText = (m_searchMode == SearchMode::LOCAL) ? " [ЛОКАЛЬНЫЙ] " : " [СЕТЬ] ";
    ftxui::Color modeColor = (m_searchMode == SearchMode::LOCAL) ? theme.accent : theme.accentCyan;

    ftxui::Element modeElem = ftxui::hbox({
        ftxui::text("Режим: ") | ftxui::color(theme.textMuted),
        ftxui::text(modeText) | ftxui::bold | ftxui::color(modeColor) | ftxui::bgcolor(theme.activeRow),
        ftxui::text(" [Tab] ") | ftxui::color(theme.textMuted)
    }) | ftxui::reflect(m_searchModeBox);

    ftxui::Element sourceElem = ftxui::hbox({
        ftxui::text("Источник: ") | ftxui::color(theme.textMuted),
        ftxui::text(" " + FormatSourceShort(m_searchSource) + " ") | ftxui::bold | ftxui::color(GetServiceColor(m_searchSource)),
        ftxui::text("  ")
    });

    ftxui::Element statsRow = ftxui::hbox({
        statsText.empty() ? ftxui::text("") : (ftxui::text(" " + statsText) | ftxui::color(theme.textMuted)),
        ftxui::filler(),
        (!m_isOnline ? (ftxui::text("[OFFLINE]  ") | ftxui::bold | ftxui::color(theme.accentRed)) : ftxui::emptyElement()),
        std::move(sourceElem),
        std::move(modeElem)
    });

    // 3. Results List
    std::vector<ftxui::Element> resultElements;

    if (m_isLoading) {
        resultElements.push_back(
            ftxui::vbox({
                ftxui::text(""),
                ftxui::text("  Загрузка результатов... [ ▰▰▰▰▱▱▱▱ ]") | ftxui::color(theme.accent),
                ftxui::text("")
            })
        );
    } else if (!m_errorMessage.empty()) {
        resultElements.push_back(
            ftxui::vbox({
                ftxui::text(""),
                ftxui::text("  [ERROR] " + m_errorMessage) | ftxui::color(theme.accentRed),
                ftxui::text("")
            })
        );
    } else if (m_searchResults.empty()) {
        resultElements.push_back(
            ftxui::vbox({
                ftxui::text(""),
                ftxui::text("  Введите запрос и нажмите Enter для поиска треков")
                    | ftxui::color(theme.textMuted),
                ftxui::text("")
            })
        );
    } else {
        int total = static_cast<int>(m_searchResults.size());
        int termHeight = ftxui::Terminal::Size().dimy;
        if (termHeight <= 0) termHeight = 30;
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
            int winH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            if (winH > 10) {
                termHeight = winH;
            }
        }
#endif
        int reserved = m_showBottomBar ? 8 : 5;
        int maxRows = std::max(4, (termHeight - reserved) / 2);

        if (m_selectedResultIndex < 0) m_selectedResultIndex = 0;
        if (m_selectedResultIndex >= total) m_selectedResultIndex = total - 1;

        int half = maxRows / 2;
        m_scrollOffset = std::clamp(m_selectedResultIndex - half, 0, std::max(0, total - maxRows));
        int end = std::min(total, m_scrollOffset + maxRows);

        int count = end - m_scrollOffset;
        m_resultRowBoxes.resize(count);
        m_likeBtnBoxes.resize(count);
        m_addBtnBoxes.resize(count);
        m_dlBtnBoxes.resize(count);
        m_visibleResultIndices.clear();

        for (int i = m_scrollOffset; i < end; ++i) {
            int rowIdx = i - m_scrollOffset;
            m_visibleResultIndices.push_back(i);
            const auto& t = m_searchResults[i];

            bool isSelected = (i == m_selectedResultIndex && m_isBrowsingResults);

            Track curTrack = m_playlist.GetCurrentTrack();
            bool isCurrentPlaying = (!curTrack.id.empty() && curTrack.id == t.id);

            std::string idxStr;
            if (isCurrentPlaying) {
                idxStr = "▶ ";
            } else {
                idxStr = (i + 1 < 10 ? "0" : "") + std::to_string(i + 1);
            }

            ftxui::Element idxElem = ftxui::text(" " + idxStr + " ")
                | (isCurrentPlaying ? (ftxui::bold | ftxui::color(theme.accent)) : ftxui::color(theme.textMuted));
            int termWidth = ftxui::Terminal::Size().dimx;
            if (termWidth <= 0) termWidth = 100;
#ifdef _WIN32
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
                int winW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
                if (winW > 20) termWidth = winW;
            }
#endif
            std::string itemSource = t.source.empty() ? m_searchSource : t.source;
            bool showSource = (m_searchSource == "All" || (m_sidebar && m_sidebar->IsSelectedPlaylist()));
            std::string shortSrc = FormatSourceShort(itemSource);
            ftxui::Element sourceBadge = showSource ? ftxui::hbox({
                ftxui::text(" " + shortSrc + " ") | ftxui::bold | ftxui::color(GetServiceColor(itemSource)),
                ftxui::text(" ")
            }) : ftxui::text("");

            int sidebarWidth = (m_sidebar && m_sidebar->IsVisible()) ? 22 : 0;
            int centerWidth = std::max(40, termWidth - sidebarWidth);
            int leftFixedW = 4 + (showSource ? 6 : 0);
            int rightFixedW = 22;
            int availCenterW = std::max(30, centerWidth - leftFixedW - rightFixedW);
            int titleWidth = std::clamp(availCenterW * 48 / 100, 20, 48);
            int plWidth = std::max(18, availCenterW - titleWidth - 4);

            std::string srcLower = itemSource;
            for (char& c : srcLower) c = std::tolower(c);
            bool isSoundCloud = (srcLower == "soundcloud" || srcLower == "sc");

            int tick = isSelected ? m_tickerTick : 0;
            ftxui::Element metaElem;
            if (isSoundCloud) {
                std::string scText = t.title.empty() ? t.artist : t.title;
                scText = ScrollText(scText, titleWidth, tick);
                metaElem = ftxui::vbox({
                    ftxui::text(scText) | ftxui::bold | ftxui::color(isSelected ? theme.accent : ftxui::Color::White),
                    ftxui::text("")
                }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, titleWidth);
            } else {
                std::string titleText = t.title.empty() ? (t.artist.empty() ? "Без названия" : t.artist) : t.title;
                std::string artistText = t.artist.empty() ? "—" : t.artist;
                titleText = ScrollText(titleText, titleWidth, tick);
                artistText = ScrollText(artistText, titleWidth, tick);
                metaElem = ftxui::vbox({
                    ftxui::text(titleText) | ftxui::bold | ftxui::color(isSelected ? theme.accent : ftxui::Color::White),
                    ftxui::text(artistText) | ftxui::color(theme.textMuted)
                }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, titleWidth);
            }

            // Middle column: Playlist membership ("Добавлен в: <playlists>" without brackets)
            std::string plLine1;
            std::string plLine2;
            auto plIt = m_trackPlaylistMap.find(t.id);
            if (plIt != m_trackPlaylistMap.end() && !plIt->second.empty()) {
                QString fullPlQ = QString::fromUtf8("Добавлен в: ") + QString::fromStdString(plIt->second);
                if (fullPlQ.length() <= plWidth) {
                    plLine1 = fullPlQ.toStdString();
                } else {
                    int splitPos = fullPlQ.lastIndexOf(", ", plWidth);
                    if (splitPos <= 12) {
                        splitPos = fullPlQ.lastIndexOf(' ', plWidth);
                    }
                    if (splitPos <= 12) {
                        splitPos = plWidth;
                    }
                    plLine1 = fullPlQ.left(splitPos).trimmed().toStdString();
                    if (splitPos < fullPlQ.length() && fullPlQ[splitPos] == ',') {
                        plLine1 += ",";
                        splitPos++;
                    }
                    QString rem = fullPlQ.mid(splitPos).trimmed();
                    if (rem.length() > plWidth) {
                        plLine2 = (rem.left(plWidth - 2).trimmed() + "..").toStdString();
                    } else {
                        plLine2 = rem.toStdString();
                    }
                }
            }

            ftxui::Element plElem = (plLine1.empty() && plLine2.empty())
                ? ftxui::emptyElement()
                : ftxui::vbox({
                    ftxui::text(plLine1) | ftxui::color(theme.accentOrange),
                    ftxui::text(plLine2) | ftxui::color(theme.accentOrange)
                }) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, plWidth);

            ftxui::Element durElem = ftxui::text(FormatTime(t.duration)) | ftxui::color(theme.textMuted);

            // Action Buttons
            bool isLiked = (m_favoriteTrackIds.count(t.id) > 0);
            std::string heartGlyph = isLiked ? " ♥ " : " ♡ ";

            bool isLikeHovered = (rowIdx < static_cast<int>(m_likeBtnBoxes.size()) && m_likeBtnBoxes[rowIdx].Contain(m_mouseX, m_mouseY));
            bool isAddHovered = (rowIdx < static_cast<int>(m_addBtnBoxes.size()) && m_addBtnBoxes[rowIdx].Contain(m_mouseX, m_mouseY));
            bool isDlHovered = (rowIdx < static_cast<int>(m_dlBtnBoxes.size()) && m_dlBtnBoxes[rowIdx].Contain(m_mouseX, m_mouseY));

            ftxui::Element dlBtn = ftxui::text(" [↓] ") | ftxui::color(theme.accentCyan);
            if (isDlHovered) {
                dlBtn = dlBtn | ftxui::bold | ftxui::bgcolor(theme.activeRow);
            }
            dlBtn = dlBtn | ftxui::reflect(m_dlBtnBoxes[rowIdx]);
            ftxui::Element likeBtn = ftxui::text(heartGlyph)
                | (isLiked ? ftxui::color(theme.accentRed) : ftxui::color(theme.textMuted));
            if (isLikeHovered) {
                likeBtn = likeBtn | ftxui::bold | ftxui::bgcolor(theme.activeRow);
            }
            likeBtn = likeBtn | ftxui::reflect(m_likeBtnBoxes[rowIdx]);

            ftxui::Element addBtn = ftxui::text(" + ")
                | ftxui::color(theme.accentCyan);
            if (isAddHovered) {
                addBtn = addBtn | ftxui::bold | ftxui::bgcolor(theme.activeRow);
            }
            addBtn = addBtn | ftxui::reflect(m_addBtnBoxes[rowIdx]);

            ftxui::Element rowElem = ftxui::hbox({
                idxElem,
                sourceBadge,
                metaElem,
                ftxui::filler(),
                plElem,
                ftxui::filler(),
                durElem,
                ftxui::text(" "),
                likeBtn,
                ftxui::text(" "),
                addBtn,
                dlBtn,
                ftxui::text(" ")
            });

            bool isRowHovered = (rowIdx < static_cast<int>(m_resultRowBoxes.size()) && m_resultRowBoxes[rowIdx].Contain(m_mouseX, m_mouseY));
            if (isSelected || isRowHovered) {
                rowElem = rowElem | ftxui::bgcolor(theme.highlight);
            }

            rowElem = rowElem | ftxui::reflect(m_resultRowBoxes[rowIdx]);
            resultElements.push_back(std::move(rowElem));
        }
        if (m_isLoadingMore) {
            resultElements.push_back(
                ftxui::hbox({
                    ftxui::text("  Загрузка следующих треков... [ ▰▰▰▱▱ ]") | ftxui::color(theme.accentCyan)
                })
            );
        }
        resultElements.push_back(ftxui::filler());
    }

    ftxui::Element resultsBox = ftxui::vbox(std::move(resultElements))
        | ftxui::flex
        | ftxui::reflect(m_resultsListBox);

    return ftxui::vbox({
        std::move(inputElem),
        ftxui::separatorLight() | ftxui::color(theme.border),
        std::move(statsRow),
        ftxui::separatorLight() | ftxui::color(theme.border),
        std::move(resultsBox)
    }) | ftxui::flex;
}

ftxui::Element SearchScreen::RenderRightColumn() {
    auto theme = m_theme;
    Track currentTrack = m_playlist.GetCurrentTrack();

    // 1. Cover Art (22x14, matches NowPlayingScreen)
    ftxui::Element coverElem = m_coverRenderer.Render(22, 14)
        | ftxui::center;

    // 2. Track Title & Artist
    std::string titleStr = currentTrack.title.empty() ? "No Track" : currentTrack.title;
    std::string artistStr = currentTrack.artist.empty() ? "—" : currentTrack.artist;
    std::string sourceStr = currentTrack.source.empty() ? "VK" : currentTrack.source;

    std::string scrollTitle = ScrollText(titleStr, 28, m_tickerTick);
    std::string scrollArtist = ScrollText(artistStr, 28, m_tickerTick);

    ftxui::Element metaElem = ftxui::vbox({
        ftxui::text(scrollTitle) | ftxui::bold | ftxui::color(theme.text),
        ftxui::text(scrollArtist) | ftxui::bold | ftxui::color(theme.accentCyan)
    });

    // 3. Audio Tech Specs Card
    ftxui::Element specsCard = ftxui::vbox({
        ftxui::text("AUDIO TECH SPECS") | ftxui::bold | ftxui::color(theme.textMuted),
        ftxui::separatorLight() | ftxui::color(theme.border),
        ftxui::hbox({
            ftxui::vbox({
                ftxui::text("КАЧЕСТВО:") | ftxui::color(theme.textMuted),
                ftxui::text("320 kbps MP3") | ftxui::bold | ftxui::color(theme.accent)
            }),
            ftxui::filler(),
            ftxui::vbox({
                ftxui::text("ИСТОЧНИК:") | ftxui::color(theme.textMuted),
                ftxui::text(FormatSourceShort(sourceStr)) | ftxui::bold | ftxui::color(GetServiceColor(sourceStr))
            })
        }),
        ftxui::text(""),
        ftxui::hbox({
            ftxui::vbox({
                ftxui::text("ФОРМАТ:") | ftxui::color(theme.textMuted),
                ftxui::text("44.1 kHz 16b") | ftxui::color(theme.text)
            }),
            ftxui::filler(),
            ftxui::vbox({
                ftxui::text("ДЛИТЕЛЬНОСТЬ:") | ftxui::color(theme.textMuted),
                ftxui::text(FormatTime(currentTrack.duration)) | ftxui::color(theme.text)
            })
        })
    });

    // 4. Mini Controls
    double currentSec = m_audio.GetPositionSeconds();
    double totalSec = m_audio.GetLengthSeconds();
    if (totalSec <= 0.0) totalSec = static_cast<double>(currentTrack.duration);
    if (currentSec < 0.0) currentSec = 0.0;

    float progress = (totalSec > 0.0) ? static_cast<float>(currentSec / totalSec) : 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);

    int barWidth = 20;
    int filled = static_cast<int>(progress * barWidth);
    std::string playedStr;
    std::string thumbStr;
    std::string remainStr;
    for (int i = 0; i < barWidth; ++i) {
        if (i < filled) playedStr += "━";
        else if (i == filled) thumbStr += "●";
        else remainStr += "─";
    }

    std::vector<ftxui::Element> barParts;
    if (!playedStr.empty()) barParts.push_back(ftxui::text(playedStr) | ftxui::color(theme.progressPlayed));
    if (!thumbStr.empty()) barParts.push_back(ftxui::text(thumbStr) | ftxui::bold | ftxui::color(theme.progressThumb));
    if (!remainStr.empty()) barParts.push_back(ftxui::text(remainStr) | ftxui::color(theme.progressRemaining));

    ftxui::Element miniSeekBar = ftxui::hbox({
        ftxui::text(FormatTime(currentSec)) | ftxui::color(theme.textMuted),
        ftxui::text(" "),
        ftxui::hbox(std::move(barParts)) | ftxui::reflect(m_miniSeekBarBox),
        ftxui::text(" "),
        ftxui::text(FormatTime(totalSec)) | ftxui::color(theme.textMuted)
    });

    bool isPlaying = m_audio.IsPlaying();
    ftxui::Element miniButtons = ftxui::hbox({
        ftxui::filler(),
        ftxui::text(" [<<] ") | ftxui::color(theme.text) | ftxui::reflect(m_miniPrevBox),
        ftxui::text(" "),
        ftxui::text(isPlaying ? " [PAUSE] " : " [PLAY] ") | ftxui::bold | ftxui::color(theme.accent) | ftxui::reflect(m_miniPlayPauseBox),
        ftxui::text(" "),
        ftxui::text(" [>>] ") | ftxui::color(theme.text) | ftxui::reflect(m_miniNextBox),
        ftxui::filler()
    });

    // 5. Volume & EQ
    int volPercent = static_cast<int>(std::round(m_audio.GetVolume() * 100.0f));
    ftxui::Element volSetting = ftxui::hbox({
        ftxui::text("ГРОМКОСТЬ: ") | ftxui::color(theme.textMuted),
        ftxui::text("[-] ") | ftxui::bold | ftxui::color(theme.accent) | ftxui::reflect(m_miniVolMinusBox),
        ftxui::text(std::to_string(volPercent) + "%") | ftxui::bold | ftxui::color(theme.accent),
        ftxui::text(" [+]") | ftxui::bold | ftxui::color(theme.accent) | ftxui::reflect(m_miniVolPlusBox)
    });

    // 6. Up Next in Queue
    int curIdx = m_playlist.GetCurrentQueueIndex();
    size_t queueSize = m_playlist.GetQueueSize();
    std::vector<ftxui::Element> upNextElems;
    upNextElems.push_back(
        ftxui::text("ДАЛЬШЕ В ОЧЕРЕДИ") | ftxui::bold | ftxui::color(theme.textMuted)
    );

    int upNextCount = 3;
    m_upNextBoxes.clear();
    m_upNextBoxes.resize(upNextCount);

    if (queueSize > 0) {
        auto nextTracks = m_playlist.GetQueueSlice(curIdx + 1, upNextCount);
        for (size_t k = 0; k < nextTracks.size(); ++k) {
            const auto& nt = nextTracks[k];
            std::string srcLower = nt.source;
            for (char& c : srcLower) c = std::tolower(c);
            bool isSoundCloud = (srcLower == "soundcloud" || srcLower == "sc");

            ftxui::Element ntMeta;
            if (isSoundCloud) {
                std::string scText = nt.title.empty() ? nt.artist : nt.title;
                scText = ScrollText(scText, 18, m_tickerTick);
                ntMeta = ftxui::text(scText) | ftxui::bold | ftxui::color(theme.text);
            } else {
                std::string titleText = nt.title.empty() ? (nt.artist.empty() ? "Без названия" : nt.artist) : nt.title;
                std::string artistText = nt.artist.empty() ? "—" : nt.artist;
                titleText = ScrollText(titleText, 18, m_tickerTick);
                artistText = ScrollText(artistText, 18, m_tickerTick);
                ntMeta = ftxui::vbox({
                    ftxui::text(titleText) | ftxui::bold | ftxui::color(theme.text),
                    ftxui::text(artistText) | ftxui::color(theme.textMuted)
                });
            }

            bool isUpNextHovered = (k < m_upNextBoxes.size() && m_upNextBoxes[k].Contain(m_mouseX, m_mouseY));
            ftxui::Element upNextRow = ftxui::hbox({
                ftxui::text(std::to_string(k + 1) + ". ") | ftxui::color(theme.textMuted),
                ntMeta | ftxui::flex,
                ftxui::filler(),
                ftxui::text(FormatTime(nt.duration)) | ftxui::color(theme.textMuted)
            });
            if (isUpNextHovered) {
                upNextRow = upNextRow | ftxui::bgcolor(theme.activeRow);
            }
            upNextRow = upNextRow | ftxui::reflect(m_upNextBoxes[k]);
            upNextElems.push_back(std::move(upNextRow));
        }
    }

    ftxui::Element upNextBox = ftxui::vbox(std::move(upNextElems));

    return ftxui::vbox({
        coverElem,
        ftxui::text(""),
        metaElem,
        ftxui::text(""),
        ftxui::separatorLight() | ftxui::color(theme.border),
        specsCard,
        ftxui::separatorLight() | ftxui::color(theme.border),
        miniSeekBar,
        ftxui::text(""),
        miniButtons,
        ftxui::text(""),
        volSetting,
        ftxui::separatorLight() | ftxui::color(theme.border),
        upNextBox,
        ftxui::filler()
    }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 27)
       | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 35);
}

ftxui::Element SearchScreen::Render() {
    auto theme = m_theme;

    return ftxui::vbox({
        ftxui::text(""),
        ftxui::hbox({
            RenderCenterColumn() | ftxui::flex,
            ftxui::separatorLight() | ftxui::color(theme.border),
            RenderRightColumn()
        }) | ftxui::flex
    }) | ftxui::flex;
}

bool SearchScreen::OnEvent(ftxui::Event event) {
    if (event.is_mouse()) {
        const auto& mouse = event.mouse();
        m_mouseX = mouse.x;
        m_mouseY = mouse.y;

        // 0. Click on Search Mode badge
        if (m_searchModeBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                ToggleSearchMode();
                return true;
            }
        }

        // 1. Click on Search Input Box
        if (m_inputBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                m_isInputActive = true;
                m_isBrowsingResults = false;
                if (m_inputComponent) m_inputComponent->TakeFocus();
                return true;
            }
        }

        // 2. Click on Like / Add buttons in search results (MUST BE BEFORE ROW CLICK!)
        for (size_t r = 0; r < m_likeBtnBoxes.size(); ++r) {
            if (m_likeBtnBoxes[r].Contain(mouse.x, mouse.y)) {
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleResultIndices.size()) {
                        int trackIdx = m_visibleResultIndices[r];
                        if (trackIdx >= 0 && trackIdx < static_cast<int>(m_searchResults.size()) && OnLikeTrack) {
                            OnLikeTrack(m_searchResults[trackIdx]);
                        }
                    }
                    return true;
                }
            }
        }
        for (size_t r = 0; r < m_addBtnBoxes.size(); ++r) {
            if (m_addBtnBoxes[r].Contain(mouse.x, mouse.y)) {
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleResultIndices.size()) {
                        int trackIdx = m_visibleResultIndices[r];
                        if (trackIdx >= 0 && trackIdx < static_cast<int>(m_searchResults.size())) {
                            if (OnAddToPlaylist) {
                                OnAddToPlaylist(m_searchResults[trackIdx]);
                            } else if (OnEnqueueTrack) {
                                OnEnqueueTrack(m_searchResults[trackIdx]);
                            }
                        }
                    }
                    return true;
                }
            }
        }
        for (size_t r = 0; r < m_dlBtnBoxes.size(); ++r) {
            if (m_dlBtnBoxes[r].Contain(mouse.x, mouse.y)) {
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleResultIndices.size()) {
                        int trackIdx = m_visibleResultIndices[r];
                        if (trackIdx >= 0 && trackIdx < static_cast<int>(m_searchResults.size()) && OnDownloadTrack) {
                            OnDownloadTrack(m_searchResults[trackIdx]);
                        }
                    }
                    return true;
                }
            }
        }

        // 3. Click on search result row -> Play track immediately!
        for (size_t r = 0; r < m_resultRowBoxes.size(); ++r) {
            if (m_resultRowBoxes[r].Contain(mouse.x, mouse.y)) {
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    if (r < m_visibleResultIndices.size()) {
                        int trackIdx = m_visibleResultIndices[r];
                        m_selectedResultIndex = trackIdx;
                        m_isBrowsingResults = true;
                        m_isInputActive = false;
                        if (trackIdx >= 0 && trackIdx < static_cast<int>(m_searchResults.size()) && OnPlayTrackNow) {
                            OnPlayTrackNow(m_searchResults[trackIdx]);
                        }
                    }
                    return true;
                }
            }
        }

        // 4. Mouse wheel over results list
        if (m_resultsListBox.Contain(mouse.x, mouse.y)) {
            m_isInputActive = false;
            m_isBrowsingResults = true;
            if (mouse.button == ftxui::Mouse::WheelUp) {
                if (m_selectedResultIndex > 0) {
                    m_selectedResultIndex--;
                    if (m_selectedResultIndex < m_scrollOffset) {
                        m_scrollOffset = m_selectedResultIndex;
                    }
                    return true;
                }
            } else if (mouse.button == ftxui::Mouse::WheelDown) {
                if (m_selectedResultIndex < static_cast<int>(m_searchResults.size()) - 1) {
                    m_selectedResultIndex++;
                    int termHeight = ftxui::Terminal::Size().dimy;
                    if (termHeight <= 0) termHeight = 30;
                    int reserved = m_showBottomBar ? 8 : 5;
                    int maxRows = std::max(4, (termHeight - reserved) / 2);
                    if (m_selectedResultIndex >= m_scrollOffset + maxRows) {
                        m_scrollOffset = m_selectedResultIndex - maxRows + 1;
                    }
                    CheckTriggerLoadMore();
                    return true;
                } else {
                    CheckTriggerLoadMore();
                }
            }
        }

        // 5. Mini player controls (seek, play/pause, prev, next, vol)
        if (m_miniSeekBarBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                int w = m_miniSeekBarBox.x_max - m_miniSeekBarBox.x_min;
                if (w > 0 && OnSeekRequested) {
                    float frac = static_cast<float>(mouse.x - m_miniSeekBarBox.x_min) / static_cast<float>(w);
                    frac = std::clamp(frac, 0.0f, 1.0f);
                    double tot = m_audio.GetLengthSeconds();
                    if (tot <= 0.0) tot = static_cast<double>(m_playlist.GetCurrentTrack().duration);
                    OnSeekRequested(frac * tot);
                    return true;
                }
            }
        }
        if (m_miniPlayPauseBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnTogglePlayPauseRequested) OnTogglePlayPauseRequested();
                return true;
            }
        }
        if (m_miniPrevBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnPrevTrackRequested) OnPrevTrackRequested();
                return true;
            }
        }
        if (m_miniNextBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                if (OnNextTrackRequested) OnNextTrackRequested();
                return true;
            }
        }
        if (m_miniVolMinusBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                float v = std::clamp(m_audio.GetVolume() - 0.05f, 0.0f, 1.0f);
                if (OnVolumeChanged) OnVolumeChanged(v);
                return true;
            }
        }
        if (m_miniVolPlusBox.Contain(mouse.x, mouse.y)) {
            if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                float v = std::clamp(m_audio.GetVolume() + 0.05f, 0.0f, 1.0f);
                if (OnVolumeChanged) OnVolumeChanged(v);
                return true;
            }
        }

        // 6. Click on Up Next track
        for (size_t k = 0; k < m_upNextBoxes.size(); ++k) {
            if (m_upNextBoxes[k].Contain(mouse.x, mouse.y)) {
                if (mouse.button == ftxui::Mouse::Left && mouse.motion == ftxui::Mouse::Pressed) {
                    int curIdx = m_playlist.GetCurrentQueueIndex();
                    int targetIdx = curIdx + 1 + static_cast<int>(k);
                    if (targetIdx < static_cast<int>(m_playlist.GetQueueSize())) {
                        if (OnPlayQueueIndexRequested) {
                            OnPlayQueueIndexRequested(targetIdx);
                        } else if (OnPlayTrackNow) {
                            auto nextTracks = m_playlist.GetQueueSlice(curIdx + 1, static_cast<int>(m_upNextBoxes.size()));
                            if (k < nextTracks.size()) {
                                OnPlayTrackNow(nextTracks[k]);
                            }
                        }
                    }
                    return true;
                }
            }
        }

        return false;
    }

    // --- KEYBOARD EVENTS ---

    // 1. If text input is active: typing into search box
    if (m_isInputActive) {
        if (event == ftxui::Event::Tab) {
            ToggleSearchMode();
            return true;
        }
        if (event == ftxui::Event::Return) {
            m_isInputActive = false;
            m_isBrowsingResults = true;
            m_selectedResultIndex = 0;
            if (m_searchMode == SearchMode::API && !m_isOnline) {
                SetSearchResults({}, "Нет подключения к сети [OFFLINE]");
                return true;
            }
            if (OnPerformSearch) {
                OnPerformSearch(m_searchQuery, m_searchSource, m_searchMode);
            }
            return true;
        }
        if (event == ftxui::Event::Escape) {
            m_isInputActive = false;
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            if (!m_searchResults.empty()) {
                m_isInputActive = false;
                m_isBrowsingResults = true;
                m_selectedResultIndex = 0;
                return true;
            }
        }
        if (m_inputComponent) {
            m_inputComponent->OnEvent(event);
            return true;
        }
        return true;
    }

    // 2. If text input is NOT active: browsing, toggling mode, and playing results
    if (event == ftxui::Event::Tab) {
        ToggleSearchMode();
        return true;
    }

    if (event == ftxui::Event::Character('j') || event == ftxui::Event::Character('J')) {
        if (!m_searchResults.empty()) {
            m_isBrowsingResults = true;
            if (m_selectedResultIndex < static_cast<int>(m_searchResults.size()) - 1) {
                m_selectedResultIndex++;
                int termHeight = ftxui::Terminal::Size().dimy;
                if (termHeight <= 0) termHeight = 30;
                int reserved = m_showBottomBar ? 8 : 5;
                int maxRows = std::max(4, (termHeight - reserved) / 2);
                if (m_selectedResultIndex >= m_scrollOffset + maxRows) {
                    m_scrollOffset = m_selectedResultIndex - maxRows + 1;
                }
            }
            CheckTriggerLoadMore();
            return true;
        }
    } else if (event == ftxui::Event::Character('k') || event == ftxui::Event::Character('K')) {
        if (!m_searchResults.empty()) {
            m_isBrowsingResults = true;
            if (m_selectedResultIndex > 0) {
                m_selectedResultIndex--;
                if (m_selectedResultIndex < m_scrollOffset) {
                    m_scrollOffset = m_selectedResultIndex;
                }
            }
            return true;
        }
    } else if (event == ftxui::Event::Return) {
        // Enter plays the selected track in search results!
        if (!m_searchResults.empty() && m_selectedResultIndex >= 0 && m_selectedResultIndex < static_cast<int>(m_searchResults.size())) {
            if (OnPlayTrackNow) {
                OnPlayTrackNow(m_searchResults[m_selectedResultIndex]);
                return true;
            }
        }
    } else if (event == ftxui::Event::Character(' ')) {
        // Space toggles Play/Pause!
        if (OnTogglePlayPauseRequested) {
            OnTogglePlayPauseRequested();
            return true;
        }
    } else if (event == ftxui::Event::Character('l') || event == ftxui::Event::Character('L')) {
        if (!m_searchResults.empty() && m_selectedResultIndex >= 0 && m_selectedResultIndex < static_cast<int>(m_searchResults.size())) {
            if (OnLikeTrack) {
                OnLikeTrack(m_searchResults[m_selectedResultIndex]);
                return true;
            }
        }
    } else if (event == ftxui::Event::Character('+') || event == ftxui::Event::Character('=')) {
        if (!m_searchResults.empty() && m_selectedResultIndex >= 0 && m_selectedResultIndex < static_cast<int>(m_searchResults.size())) {
            if (OnAddToPlaylist) {
                OnAddToPlaylist(m_searchResults[m_selectedResultIndex]);
                return true;
            } else if (OnEnqueueTrack) {
                OnEnqueueTrack(m_searchResults[m_selectedResultIndex]);
                return true;
            }
        }
    } else if (event == ftxui::Event::Character('f') || event == ftxui::Event::Character('F')) {
        FocusSearchInput();
        return true;
    }

    return false;
}

} // namespace tui
