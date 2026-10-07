#pragma once

#include <QAbstractListModel>
#include <vector>
#include "models/Track.h"

namespace ui::android {

class TrackListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum TrackRoles {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        DurationStrRole,
        DurationRole,
        CoverUrlRole,
        SourceRole,
        IsPlayingRole,
        IndexRole
    };

    explicit TrackListModel(QObject* parent = nullptr);
    ~TrackListModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void SetTracks(const std::vector<Track>& tracks);
    void AppendTracks(const std::vector<Track>& tracks);
    void ClearTracks();
    void SetPlayingIndex(int index);

    const std::vector<Track>& GetTracks() const { return m_tracks; }
    const Track* GetTrackAt(int index) const;

signals:
    void tracksCountChanged(int count);

private:
    std::vector<Track> m_tracks;
    int m_playingIndex{-1};
};

} // namespace ui::android
