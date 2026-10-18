#include "TrackListModel.h"
#include "services/cache/CoverCacheService.h"
#include <random>
#include <algorithm>

namespace ui::android {

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent) {
    connect(&services::cache::CoverCacheService::Instance(), &services::cache::CoverCacheService::CoverCached,
            this, [this](const QString& remoteUrl, const QString& /*localFileUrl*/) {
        std::string rUrl = remoteUrl.toStdString();
        auto range = m_coverUrlToRowIndex.equal_range(rUrl);
        for (auto it = range.first; it != range.second; ++it) {
            int row = it->second;
            if (row >= 0 && row < static_cast<int>(m_tracks.size())) {
                emit dataChanged(index(row), index(row), {CoverUrlRole, ThumbnailUrlRole});
            }
        }
    });
}

void TrackListModel::rebuildCoverIndex() {
    m_coverUrlToRowIndex.clear();
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        const std::string& cov = m_tracks[i].GetDisplayCoverUrl(true);
        if (!cov.empty()) m_coverUrlToRowIndex.emplace(cov, static_cast<int>(i));
        const std::string& thumb = m_tracks[i].GetDisplayCoverUrl(false);
        if (!thumb.empty() && thumb != cov) m_coverUrlToRowIndex.emplace(thumb, static_cast<int>(i));
    }
}

int TrackListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_tracks.size());
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) return {};

    const auto& track = m_tracks[row];
    switch (role) {
        case TitleRole:
            return QString::fromStdString(track.title);
        case ArtistRole:
            return QString::fromStdString(track.artist);
        case DurationStrRole:
            return QString::fromStdString(track.GetFormattedDuration());
        case DurationRole:
            return track.duration;
        case CoverUrlRole:
            return services::cache::CoverCacheService::Instance().GetCoverUrl(
                QString::fromStdString(track.GetDisplayCoverUrl(true)), false);
        case ThumbnailUrlRole:
            return services::cache::CoverCacheService::Instance().GetCoverUrl(
                QString::fromStdString(track.GetDisplayCoverUrl(false)), true);
        case SourceRole:
            return QString::fromStdString(track.source);
        case IsPlayingRole:
            return (row == m_playingIndex);
        case IsLibraryRole:
            return track.isLibrary;
        case IndexRole:
            return row;
        default:
            return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[TitleRole] = "title";
    roles[ArtistRole] = "artist";
    roles[DurationStrRole] = "durationStr";
    roles[DurationRole] = "duration";
    roles[CoverUrlRole] = "coverUrl";
    roles[ThumbnailUrlRole] = "thumbnailUrl";
    roles[SourceRole] = "source";
    roles[IsPlayingRole] = "isPlaying";
    roles[IsLibraryRole] = "isLibrary";
    roles[IndexRole] = "index";
    return roles;
}

void TrackListModel::SetTracks(const std::vector<Track>& tracks) {
    beginResetModel();
    m_tracks = tracks;
    m_playingIndex = -1;
    rebuildCoverIndex();
    endResetModel();
    emit tracksCountChanged(static_cast<int>(m_tracks.size()));

    if (m_previewModel) {
        std::vector<Track> preview;
        size_t count = std::min(m_tracks.size(), size_t(8));
        preview.assign(m_tracks.begin(), m_tracks.begin() + count);
        m_previewModel->SetTracks(preview);
    }
}

void TrackListModel::AppendTracks(const std::vector<Track>& tracks) {
    if (tracks.empty()) return;
    int first = static_cast<int>(m_tracks.size());
    int last = first + static_cast<int>(tracks.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_tracks.insert(m_tracks.end(), tracks.begin(), tracks.end());
    rebuildCoverIndex();
    endInsertRows();
    emit tracksCountChanged(static_cast<int>(m_tracks.size()));

    if (m_previewModel && m_previewModel->rowCount() < 8) {
        std::vector<Track> preview;
        size_t count = std::min(m_tracks.size(), size_t(8));
        preview.assign(m_tracks.begin(), m_tracks.begin() + count);
        m_previewModel->SetTracks(preview);
    }
}

void TrackListModel::ClearTracks() {
    beginResetModel();
    m_tracks.clear();
    m_playingIndex = -1;
    m_coverUrlToRowIndex.clear();
    endResetModel();
    emit tracksCountChanged(0);

    if (m_previewModel) {
        m_previewModel->ClearTracks();
    }
}

void TrackListModel::SetPlayingIndex(int index) {
    if (m_playingIndex == index) return;
    int oldIdx = m_playingIndex;
    m_playingIndex = index;
    if (oldIdx >= 0 && oldIdx < static_cast<int>(m_tracks.size())) {
        emit dataChanged(this->index(oldIdx), this->index(oldIdx), {IsPlayingRole});
    }
    if (m_playingIndex >= 0 && m_playingIndex < static_cast<int>(m_tracks.size())) {
        emit dataChanged(this->index(m_playingIndex), this->index(m_playingIndex), {IsPlayingRole});
    }

    if (m_previewModel) {
        m_previewModel->SetPlayingIndex(index >= 0 && index < 8 ? index : -1);
    }
}

const Track* TrackListModel::GetTrackAt(int index) const {
    if (index >= 0 && index < static_cast<int>(m_tracks.size())) {
        return &m_tracks[index];
    }
    return nullptr;
}

void TrackListModel::UpdateTrackLibraryStatus(const std::string& trackId, bool isLibrary) {
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == trackId) {
            m_tracks[i].isLibrary = isLibrary;
            emit dataChanged(this->index(static_cast<int>(i)), this->index(static_cast<int>(i)), {IsLibraryRole});
            break;
        }
    }

    if (m_previewModel) {
        m_previewModel->UpdateTrackLibraryStatus(trackId, isLibrary);
    }
}

void TrackListModel::Sort(const QString& mode) {
    if (m_tracks.empty()) return;
    beginResetModel();
    if (mode == "date-desc") {
        std::stable_sort(m_tracks.begin(), m_tracks.end(), [](const Track& a, const Track& b) {
            return a.dbId > b.dbId;
        });
    } else if (mode == "date-asc") {
        std::stable_sort(m_tracks.begin(), m_tracks.end(), [](const Track& a, const Track& b) {
            return a.dbId < b.dbId;
        });
    } else if (mode == "title-asc") {
        std::stable_sort(m_tracks.begin(), m_tracks.end(), [](const Track& a, const Track& b) {
            return QString::fromStdString(a.title).localeAwareCompare(QString::fromStdString(b.title)) < 0;
        });
    } else if (mode == "artist-asc") {
        std::stable_sort(m_tracks.begin(), m_tracks.end(), [](const Track& a, const Track& b) {
            return QString::fromStdString(a.artist).localeAwareCompare(QString::fromStdString(b.artist)) < 0;
        });
    }
    m_playingIndex = -1;
    rebuildCoverIndex();
    endResetModel();

    if (m_previewModel) {
        std::vector<Track> preview;
        size_t count = std::min(m_tracks.size(), size_t(8));
        preview.assign(m_tracks.begin(), m_tracks.begin() + count);
        m_previewModel->SetTracks(preview);
    }
}

void TrackListModel::Shuffle() {
    if (m_tracks.size() <= 1) return;
    beginResetModel();
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(m_tracks.begin(), m_tracks.end(), g);
    m_playingIndex = -1;
    rebuildCoverIndex();
    endResetModel();

    if (m_previewModel) {
        std::vector<Track> preview;
        size_t count = std::min(m_tracks.size(), size_t(8));
        preview.assign(m_tracks.begin(), m_tracks.begin() + count);
        m_previewModel->SetTracks(preview);
    }
}

} // namespace ui::android
