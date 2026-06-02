#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QVariantList>
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include "core/providers/emby/embyclient.h"
#include "core/cache/cachestore.h"
#include "core/media/models/mediamodel.h"

class LibraryBrowser : public QObject {
    Q_OBJECT
    Q_PROPERTY(MediaModel* libraryModel READ libraryModel CONSTANT)
    Q_PROPERTY(MediaModel* contentModel READ contentModel CONSTANT)
    Q_PROPERTY(MediaModel* searchModel READ searchModel CONSTANT)
    Q_PROPERTY(MediaModel* genreModel READ genreModel CONSTANT)
    Q_PROPERTY(MediaModel* studioModel READ studioModel CONSTANT)
    Q_PROPERTY(MediaModel* suggestionsResumeModel READ suggestionsResumeModel CONSTANT)
    Q_PROPERTY(MediaModel* suggestionsLatestModel READ suggestionsLatestModel CONSTANT)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY currentTabChanged)
    Q_PROPERTY(QString browseContext READ browseContext NOTIFY browseContextChanged)
    Q_PROPERTY(QString currentLibraryId READ currentLibraryId NOTIFY currentLibraryIdChanged)
    Q_PROPERTY(QString currentLibraryType READ currentLibraryType NOTIFY currentLibraryIdChanged)
    Q_PROPERTY(int movieCount READ movieCount NOTIFY movieCountChanged)
    Q_PROPERTY(int seriesCount READ seriesCount NOTIFY seriesCountChanged)
    Q_PROPERTY(int episodeCount READ episodeCount NOTIFY episodeCountChanged)
    Q_PROPERTY(int totalItems READ totalItems NOTIFY totalItemsChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY totalItemsChanged)
    Q_PROPERTY(QVariantList latestSections READ latestSections NOTIFY latestSectionsChanged)
    Q_PROPERTY(int sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortAscendingChanged)
    Q_PROPERTY(bool filterFavorites READ filterFavorites WRITE setFilterFavorites NOTIFY filterFavoritesChanged)
    Q_PROPERTY(int filterPlayed READ filterPlayed WRITE setFilterPlayed NOTIFY filterPlayedChanged)
    Q_PROPERTY(MediaModel* favMoviesModel READ favMoviesModel CONSTANT)
    Q_PROPERTY(MediaModel* favSeriesModel READ favSeriesModel CONSTANT)
    Q_PROPERTY(MediaModel* favEpisodesModel READ favEpisodesModel CONSTANT)
    Q_PROPERTY(MediaModel* favPeopleModel READ favPeopleModel CONSTANT)
    Q_PROPERTY(MediaModel* searchMoviesModel READ searchMoviesModel CONSTANT)
    Q_PROPERTY(MediaModel* searchSeriesModel READ searchSeriesModel CONSTANT)
    Q_PROPERTY(MediaModel* searchEpisodesModel READ searchEpisodesModel CONSTANT)
    Q_PROPERTY(MediaModel* searchPeopleModel READ searchPeopleModel CONSTANT)
    Q_PROPERTY(MediaModel* recommendModel READ recommendModel CONSTANT)

public:
    enum BrowseTab {
        TabDefault, TabSuggestions, TabTrailers, TabFavorites,
        TabGenres, TabStudios, TabEpisodes, TabFolders
    };
    Q_ENUM(BrowseTab)

    explicit LibraryBrowser(EmbyClient *emby, CacheStore *cache, QObject *parent = nullptr);

    MediaModel *libraryModel() const;
    MediaModel *contentModel() const;
    MediaModel *searchModel() const;
    MediaModel *genreModel() const;
    MediaModel *studioModel() const;
    MediaModel *suggestionsResumeModel() const;
    MediaModel *suggestionsLatestModel() const;
    int currentTab() const;
    QString browseContext() const { return m_browseContext; }
    QString currentLibraryId() const;
    QString currentLibraryType() const;
    int movieCount() const { return m_movieCount; }
    int seriesCount() const { return m_seriesCount; }
    int episodeCount() const { return m_episodeCount; }
    int totalItems() const { return m_totalItems; }
    bool canLoadMore() const;
    QVariantList latestSections() const;
    int sortBy() const { return m_sortBy; }
    void setSortBy(int v);
    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool v);
    bool filterFavorites() const { return m_filterFavorites; }
    void setFilterFavorites(bool v);
    int filterPlayed() const { return m_filterPlayed; }
    void setFilterPlayed(int v);
    MediaModel *favMoviesModel() const { return m_favMoviesModel; }
    MediaModel *favSeriesModel() const { return m_favSeriesModel; }
    MediaModel *favEpisodesModel() const { return m_favEpisodesModel; }
    MediaModel *favPeopleModel() const { return m_favPeopleModel; }
    MediaModel *searchMoviesModel() const { return m_searchMoviesModel; }
    MediaModel *searchSeriesModel() const { return m_searchSeriesModel; }
    MediaModel *searchEpisodesModel() const { return m_searchEpisodesModel; }
    MediaModel *searchPeopleModel() const { return m_searchPeopleModel; }
    MediaModel *recommendModel() const { return m_recommendModel; }

public slots:
    void onLibrariesFetched(const QJsonArray &libraries);
    Q_INVOKABLE void browseLibrary(const QString &libraryId);
    Q_INVOKABLE void setLibraryTab(int tab);
    Q_INVOKABLE void browseGenre(const QString &genreId, const QString &genreName);
    Q_INVOKABLE void browseStudio(const QString &studioId, const QString &studioName);
    Q_INVOKABLE void browsePerson(const QString &personId, const QString &personName);
    Q_INVOKABLE void loadMoreEpisodes();
    Q_INVOKABLE void fetchHome();
    Q_INVOKABLE void refreshResume() { m_emby->fetchResume(12); }
    Q_INVOKABLE void fetchFavorites(const QString &libraryId = QString());
    Q_INVOKABLE void search(const QString &term);
    Q_INVOKABLE void fetchRecommendations();
    Q_INVOKABLE void searchHints(const QString &term) { m_emby->searchHints(term); }
    Q_INVOKABLE void updateCardField(const QString &itemId, const QString &roleName, const QVariant &value);
    Q_INVOKABLE void applySortAndFilter();
    void clearAll();

signals:
    void currentLibraryIdChanged();
    void currentTabChanged();
    void browseContextChanged();
    void totalItemsChanged();
    void movieCountChanged();
    void seriesCountChanged();
    void episodeCountChanged();
    void latestSectionsChanged();
    void sortByChanged();
    void sortAscendingChanged();
    void filterFavoritesChanged();
    void filterPlayedChanged();
    void searchHintsFetched(const QJsonArray &hints);
    void personBrowseStarted(const QString &name);

private:
    void onItemsFetched(const QJsonArray &items, const QString &parentId, const QString &cacheKey, int totalRecordCount);
    QString currentSortByString() const;
    QString buildFiltersString() const;

    EmbyClient *m_emby;
    CacheStore *m_cache;
    MediaModel *m_libraryModel;
    MediaModel *m_contentModel;
    MediaModel *m_searchModel;
    MediaModel *m_genreModel;
    MediaModel *m_studioModel;
    MediaModel *m_suggestionsResumeModel;
    MediaModel *m_suggestionsLatestModel;
    int m_currentTab = 0;
    QString m_browseContext;
    QMap<QString, MediaModel *> m_latestModels;
    int m_pendingLatestSections = 0;
    QString m_currentLibraryId;
    QHash<QString, QString> m_libraryTypes;
    int m_movieCount = 0;
    int m_seriesCount = 0;
    int m_episodeCount = 0;
    int m_totalItems = -1;
    int m_paginationLimit = 0;
    bool m_loadingMore = false;
    bool m_pendingStudioSwitch = false;
    bool m_pendingGenreSwitch = false;
    MediaModel *m_favMoviesModel;
    MediaModel *m_favSeriesModel;
    MediaModel *m_favEpisodesModel;
    MediaModel *m_favPeopleModel;
    bool m_fetchingFavPersons = false;
    MediaModel *m_searchMoviesModel;
    MediaModel *m_searchSeriesModel;
    MediaModel *m_searchEpisodesModel;
    MediaModel *m_searchPeopleModel;
    MediaModel *m_recommendModel;
    bool m_fetchingSearchPersons = false;
    int m_sortBy = 0;
    bool m_sortAscending = true;
    bool m_filterFavorites = false;
    int m_filterPlayed = 0; // 0=all, 1=played, 2=unplayed
};
