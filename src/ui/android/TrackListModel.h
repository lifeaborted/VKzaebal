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
        ThumbnailUrlRole,
        SourceRole,
        IsPlayingRole,
        IsLibraryRole,
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
    void UpdateTrackLibraryStatus(const std::string& trackId, bool isLibrary);
    void Sort(const QString& mode);
    void Shuffle();

    void SetPreviewModel(TrackListModel* previewModel) { m_previewModel = previewModel; }

    const std::vector<Track>& GetTracks() const { return m_tracks; }
    const Track* GetTrackAt(int index) const;

signals:
    void tracksCountChanged(int count);

private:
    void rebuildCoverIndex();

    std::vector<Track> m_tracks;
    int m_playingIndex{-1};
    std::unordered_multimap<std::string, int> m_coverUrlToRowIndex;
    TrackListModel* m_previewModel{nullptr};
};

} // namespace ui::android
