#include "TrackListModel.h"

namespace ui::android {

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent) {
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
            return QString::fromStdString(track.coverUrl);
        case SourceRole:
            return QString::fromStdString(track.source);
        case IsPlayingRole:
            return (row == m_playingIndex);
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
    roles[SourceRole] = "source";
    roles[IsPlayingRole] = "isPlaying";
    roles[IndexRole] = "index";
    return roles;
}

void TrackListModel::SetTracks(const std::vector<Track>& tracks) {
    beginResetModel();
    m_tracks = tracks;
    m_playingIndex = -1;
    endResetModel();
    emit tracksCountChanged(static_cast<int>(m_tracks.size()));
}

void TrackListModel::AppendTracks(const std::vector<Track>& tracks) {
    if (tracks.empty()) return;
    int first = static_cast<int>(m_tracks.size());
    int last = first + static_cast<int>(tracks.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_tracks.insert(m_tracks.end(), tracks.begin(), tracks.end());
    endInsertRows();
    emit tracksCountChanged(static_cast<int>(m_tracks.size()));
}

void TrackListModel::ClearTracks() {
    beginResetModel();
    m_tracks.clear();
    m_playingIndex = -1;
    endResetModel();
    emit tracksCountChanged(0);
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
}

const Track* TrackListModel::GetTrackAt(int index) const {
    if (index >= 0 && index < static_cast<int>(m_tracks.size())) {
        return &m_tracks[index];
    }
    return nullptr;
}

} // namespace ui::android
