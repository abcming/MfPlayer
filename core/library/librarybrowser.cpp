#include "core/library/librarybrowser.h"
#include "core/media/models/mediamodel.h"
#include "common/constants.h"
#include <QSet>
#include <QTimer>

LibraryBrowser::LibraryBrowser(EmbyClient *emby, CacheStore *cache, QObject *parent)
    : QObject(parent)
    , m_emby(emby)
    , m_cache(cache)
    , m_libraryModel(new MediaModel(this))
    , m_contentModel(new MediaModel(this))
    , m_searchModel(new MediaModel(this))
    , m_genreModel(new MediaModel(this))
    , m_studioModel(new MediaModel(this))
    , m_suggestionsResumeModel(new MediaModel(this))
    , m_suggestionsLatestModel(new MediaModel(this))
    , m_favMoviesModel(new MediaModel(this))
    , m_favSeriesModel(new MediaModel(this))
    , m_favEpisodesModel(new MediaModel(this))
    , m_favPeopleModel(new MediaModel(this))
    , m_searchMoviesModel(new MediaModel(this))
    , m_searchSeriesModel(new MediaModel(this))
    , m_searchEpisodesModel(new MediaModel(this))
    , m_searchPeopleModel(new MediaModel(this))
    , m_recommendModel(new MediaModel(this))
{
    // Search debounce: batch rapid keystrokes into a single API call
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(300);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        executeSearch(m_pendingSearchTerm);
    });

    connect(m_emby, &EmbyClient::itemsFetched, this, &LibraryBrowser::onItemsFetched);

    connect(m_emby, &EmbyClient::latestFetched, this, [this](const QJsonArray &items, const QString &tag) {
        auto it = m_latestModels.find(tag);
        if (it != m_latestModels.end())
            it.value()->setItems(items);
        if (--m_pendingLatestSections <= 0) {
            m_pendingLatestSections = 0;
            QTimer::singleShot(0, this, [this]() { emit latestSectionsChanged(); });
        }
    });

    connect(m_emby, &EmbyClient::searchHintsFetched, this, [this](const QJsonArray &hints) {
        m_searchModel->setItems(hints);
        emit searchHintsFetched(hints);
    });

    connect(m_emby, &EmbyClient::genresFetched, this, [this](const QJsonArray &genres) {
        m_genreModel->setItems(genres);
        if (m_currentTab == TabGenres)
            m_contentModel->setItems(genres);
    });

    connect(m_emby, &EmbyClient::studiosFetched, this, [this](const QJsonArray &studios) {
        m_studioModel->setItems(studios);
        if (m_currentTab == TabStudios)
            m_contentModel->setItems(studios);
    });

    connect(m_emby, &EmbyClient::suggestionsResumeFetched, this, [this](const QJsonArray &items) {
        m_suggestionsResumeModel->setItems(items);
    });

    connect(m_emby, &EmbyClient::suggestionsLatestFetched, this, [this](const QJsonArray &items) {
        m_suggestionsLatestModel->setItems(items);
    });


    connect(m_emby, &EmbyClient::favoriteChanged, this, [this](const QString &itemId, bool isFavorite) {
        updateCardField(itemId, "isFavorite", isFavorite);
        if (!isFavorite) {
            m_favMoviesModel->removeItem(itemId);
            m_favSeriesModel->removeItem(itemId);
            m_favEpisodesModel->removeItem(itemId);
            m_favPeopleModel->removeItem(itemId);
        }
    });

    connect(m_emby, &EmbyClient::playedStatusChanged, this, [this](const QString &itemId, bool played) {
        updateCardField(itemId, "played", played);
        // Re-fetch suggestions resume so fully-watched items disappear from the list
        m_emby->fetchSuggestionsResume(m_currentLibraryId);
    });

    connect(m_emby, &EmbyClient::itemCountsFetched, this, [this](int movie, int series, int episodes) {
        m_movieCount = movie;
        m_seriesCount = series;
        m_episodeCount = episodes;
        emit movieCountChanged();
        emit seriesCountChanged();
        emit episodeCountChanged();
    });
}

MediaModel *LibraryBrowser::libraryModel() const { return m_libraryModel; }
MediaModel *LibraryBrowser::contentModel() const { return m_contentModel; }
MediaModel *LibraryBrowser::searchModel() const { return m_searchModel; }
MediaModel *LibraryBrowser::genreModel() const { return m_genreModel; }
MediaModel *LibraryBrowser::studioModel() const { return m_studioModel; }
MediaModel *LibraryBrowser::suggestionsResumeModel() const { return m_suggestionsResumeModel; }
MediaModel *LibraryBrowser::suggestionsLatestModel() const { return m_suggestionsLatestModel; }
int LibraryBrowser::currentTab() const { return m_currentTab; }
QString LibraryBrowser::currentLibraryId() const { return m_currentLibraryId; }
QString LibraryBrowser::currentLibraryType() const { return m_libraryTypes.value(m_currentLibraryId); }

bool LibraryBrowser::canLoadMore() const {
    return m_paginationLimit > 0 && m_contentModel->rowCount() < m_totalItems;
}

QVariantList LibraryBrowser::latestSections() const {
    QVariantList sections;
    int count = m_libraryModel->rowCount();
    for (int i = 0; i < count; ++i) {
        QVariantMap lib = m_libraryModel->get(i);
        QString libId = lib["itemId"].toString();
        auto it = m_latestModels.find(libId);
        if (it != m_latestModels.end() && it.value()->rowCount() > 0) {
            QVariantMap section;
            section["name"] = lib["itemName"].toString();
            section["model"] = QVariant::fromValue(it.value());
            sections.append(section);
        }
    }
    return sections;
}

void LibraryBrowser::onLibrariesFetched(const QJsonArray &libraries) {
    m_libraryTypes.clear();
    for (const auto &lib : libraries) {
        auto obj = lib.toObject();
        m_libraryTypes[obj["Id"].toString()] = obj["CollectionType"].toString();
    }
    m_libraryModel->setItems(libraries);
    if (!libraries.isEmpty())
        browseLibrary(libraries.first().toObject()["Id"].toString());
    fetchHome();
}

void LibraryBrowser::browseLibrary(const QString &libraryId) {
    m_currentLibraryId = libraryId;
    m_currentTab = TabDefault;
    m_browseContext.clear();
    m_contentModel->clear();
    ++m_browseGeneration;
    m_loadingMore = false;
    m_paginationLimit = kPageSize;
    m_totalItems = -1;
    emit currentLibraryIdChanged();
    emit currentTabChanged();
    emit totalItemsChanged();

    // Reset filters on library switch (keep sort preference)
    m_filterFavorites = false;
    m_filterPlayed = 0;
    emit filterFavoritesChanged();
    emit filterPlayedChanged();

    QString includeTypes = m_libraryTypes.value(libraryId) == "movies" ? Constants::kTypeMovie : Constants::kTypeSeries;
    FetchParams p{
        .parentId = libraryId,
        .includeTypes = includeTypes,
        .filters = buildFiltersString(),
        .sortBy = currentSortByString(),
        .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
        .limit = m_paginationLimit
    };
    // 缓存预显示: 首屏立即用上次的数据渲染, 网络数据到达后由 setItems 覆盖
    // (数据未变时 MediaModel 指纹去重会跳过 reset, 零闪烁)
    QJsonArray cached = m_cache->getItems(p.cacheKey());
    if (!cached.isEmpty())
        m_contentModel->setItems(cached);
    dispatchFetch(p);
}

void LibraryBrowser::setLibraryTab(int tab) {
    if (m_currentLibraryId.isEmpty()) return;
    m_currentTab = tab;
    m_browseContext.clear();
    m_contentModel->clear();
    ++m_browseGeneration;
    m_loadingMore = false;
    m_paginationLimit = 0;
    m_totalItems = -1;
    emit currentTabChanged();
    emit totalItemsChanged();
    emit browseContextChanged();

    switch (tab) {
    case TabDefault: {
        m_paginationLimit = kPageSize;
        FetchParams p{
            .parentId = m_currentLibraryId,
            .includeTypes = m_libraryTypes.value(m_currentLibraryId) == "movies" ? Constants::kTypeMovie : Constants::kTypeSeries,
            .filters = buildFiltersString(),
            .sortBy = currentSortByString(),
            .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
            .limit = m_paginationLimit
        };
        // 缓存预显示 — 同 browseLibrary
        QJsonArray cached = m_cache->getItems(p.cacheKey());
        if (!cached.isEmpty())
            m_contentModel->setItems(cached);
        dispatchFetch(p);
        break;
    }
    case TabSuggestions:
        m_emby->fetchSuggestionsResume(m_currentLibraryId);
        m_emby->fetchSuggestionsLatest(m_currentLibraryId,
            m_libraryTypes.value(m_currentLibraryId) == "movies" ? Constants::kTypeMovie : Constants::kTypeEpisode);
        break;
    case TabTrailers:
        break;
    case TabFavorites:
        fetchFavorites(m_currentLibraryId);
        break;
    case TabGenres:
        m_emby->fetchGenres(m_currentLibraryId);
        break;
    case TabStudios:
        m_emby->fetchStudios(m_currentLibraryId);
        break;
    case TabEpisodes:
        m_paginationLimit = kPageSize;
        m_totalItems = -1;
        emit totalItemsChanged();
        dispatchFetch({
            .parentId = m_currentLibraryId,
            .includeTypes = Constants::kTypeEpisode,
            .filters = buildFiltersString(),
            .sortBy = currentSortByString(),
            .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
            .limit = m_paginationLimit
        });
        break;
    }
}

// 结果要进 m_contentModel 的分发一律走这里 —— 它把请求的 cacheKey 记成"当前期望",
// onItemsFetched 拿它当请求身份比对。绕过这个函数直接调 m_emby->fetchItemsFiltered()
// 就等于没有守卫, 响应会被无条件填进列表
void LibraryBrowser::dispatchFetch(const FetchParams &p) {
    m_currentFetchKey = p.cacheKey();
    m_emby->fetchItemsFiltered(p);
}

void LibraryBrowser::loadMore() {
    if (m_paginationLimit <= 0 || m_loadingMore) return;
    int loaded = m_contentModel->rowCount();
    if (m_totalItems >= 0 && loaded >= m_totalItems) return;
    m_loadingMore = true;
    QString includeTypes = m_currentTab == TabEpisodes
        ? Constants::kTypeEpisode
        : (m_libraryTypes.value(m_currentLibraryId) == "movies" ? Constants::kTypeMovie
                                                                : Constants::kTypeSeries);
    // 回调版绕过 itemsFetched 分发: 续拉批次不覆盖缓存里的首屏数据,
    // gen 比对丢弃列表切换后仍在飞行的旧批次
    uint32_t gen = m_browseGeneration;
    m_emby->fetchItemsFiltered({
        .parentId = m_currentLibraryId,
        .includeTypes = includeTypes,
        .filters = buildFiltersString(),
        .sortBy = currentSortByString(),
        .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
        .limit = m_paginationLimit,
        .startIndex = loaded
    }, [this, gen](const QJsonArray &items) {
        m_loadingMore = false;
        if (gen != m_browseGeneration) return;
        if (items.isEmpty()) {
            // 服务器实际条数少于 TotalRecordCount — 修正并停止续拉
            m_totalItems = m_contentModel->rowCount();
            emit totalItemsChanged();
            return;
        }
        m_contentModel->appendItems(items);
        // TabDefault 自动渐进拉满 (字母索引需要全量); Episodes 保持滚动触发
        if (m_currentTab == TabDefault && canLoadMore())
            loadMore();
    });
}

void LibraryBrowser::browseGenre(const QString &genreId, const QString &genreName) {
    if (m_currentLibraryId.isEmpty()) return;
    m_browseContext = genreName;
    m_pendingGenreSwitch = true;
    ++m_browseGeneration;
    m_loadingMore = false;
    m_paginationLimit = 0;  // genre 结果全量返回, 不续拉
    emit browseContextChanged();
    dispatchFetch({.parentId = m_currentLibraryId,
        .includeTypes = m_libraryTypes.value(m_currentLibraryId) == "movies" ? Constants::kTypeMovie : Constants::kTypeSeries,
        .filters = buildFiltersString(),
        .sortBy = currentSortByString(),
        .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
        .genreIds = genreId});
}

void LibraryBrowser::browseStudio(const QString &studioId, const QString &studioName) {
    if (m_currentLibraryId.isEmpty()) return;
    m_browseContext = studioName;
    m_pendingStudioSwitch = true;
    ++m_browseGeneration;
    m_loadingMore = false;
    m_paginationLimit = 0;  // studio 结果全量返回, 不续拉
    emit browseContextChanged();
    dispatchFetch({.parentId = m_currentLibraryId,
        .includeTypes = m_libraryTypes.value(m_currentLibraryId) == "movies" ? Constants::kTypeMovie : Constants::kTypeSeries,
        .filters = buildFiltersString(),
        .sortBy = currentSortByString(),
        .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
        .studioIds = studioId});
}

void LibraryBrowser::fetchHome() {
    m_emby->fetchItemCounts();
    m_emby->fetchResume(12);

    int count = m_libraryModel->rowCount();
    m_pendingLatestSections = count;

    // Collect current library IDs
    QSet<QString> currentLibIds;
    for (int i = 0; i < count; ++i) {
        QVariantMap lib = m_libraryModel->get(i);
        currentLibIds.insert(lib["itemId"].toString());
    }

    // Delete models for libraries that no longer exist; clear the rest for reuse
    for (auto it = m_latestModels.begin(); it != m_latestModels.end(); ) {
        if (!currentLibIds.contains(it.key())) {
            delete it.value();
            it = m_latestModels.erase(it);
        } else {
            it.value()->clear();
            ++it;
        }
    }

    for (int i = 0; i < count; ++i) {
        QVariantMap lib = m_libraryModel->get(i);
        QString libId = lib["itemId"].toString();
        QString libType = lib["itemType"].toString();

        // Reuse existing model, or create a new one only if missing
        if (!m_latestModels.contains(libId))
            m_latestModels[libId] = new MediaModel(this);

        QString types = (libType == "CollectionFolder") ? "Movie,Series" : "Movie,Series";
        m_emby->fetchLatest(12, libId, types, libId);
    }

    if (count == 0)
        emit latestSectionsChanged();

    // Safety: force emit after 5s even if some fetchLatest calls fail
    if (count > 0) {
        QTimer::singleShot(5000, this, [this]() {
            if (m_pendingLatestSections > 0) {
                m_pendingLatestSections = 0;
                emit latestSectionsChanged();
            }
        });
    }
}

void LibraryBrowser::fetchFavorites(const QString &libraryId) {
    m_favMoviesModel->clear();
    m_favSeriesModel->clear();
    m_favEpisodesModel->clear();
    m_favPeopleModel->clear();
    // Direct callbacks bypass itemsFetched dispatch — no ordering/interference issues
    m_emby->fetchItemsFiltered({.parentId = libraryId, .includeTypes = Constants::kTypeMovie, .filters = "IsFavorite", .limit = 50},
        [this](const QJsonArray &items) { m_favMoviesModel->setItems(items); });
    m_emby->fetchItemsFiltered({.parentId = libraryId, .includeTypes = Constants::kTypeSeries, .filters = "IsFavorite", .limit = 50},
        [this](const QJsonArray &items) { m_favSeriesModel->setItems(items); });
    m_emby->fetchItemsFiltered({.parentId = libraryId, .includeTypes = Constants::kTypeEpisode, .filters = "IsFavorite", .limit = 50},
        [this](const QJsonArray &items) { m_favEpisodesModel->setItems(items); });
    m_emby->fetchFavPersons(50, [this](const QJsonArray &items) {
        m_favPeopleModel->setItems(items);
    });
}

void LibraryBrowser::search(const QString &term) {
    if (term.length() < 2) {
        m_searchDebounceTimer->stop();
        // 停 timer 只能拦住"还没发出去"的。已经在飞的请求要靠 ++gen 作废 ——
        // 否则输入"钢铁侠"发出请求、删到"钢"清空模型, 旧结果回来照样 setItems,
        // 搜索框空着列表却有内容
        ++m_searchGeneration;
        m_searchMoviesModel->clear();
        m_searchSeriesModel->clear();
        m_searchPeopleModel->clear();
        return;
    }
    m_pendingSearchTerm = term;
    m_searchDebounceTimer->start();  // restarts on each call (300ms debounce)
}

void LibraryBrowser::executeSearch(const QString &term) {
    if (term.length() < 2) return;
    // 每次新搜索都作废上一次在飞的三个请求, 不然连续输入时先后顺序不保证
    uint32_t gen = ++m_searchGeneration;
    m_searchMoviesModel->clear();
    m_searchSeriesModel->clear();
    m_searchPeopleModel->clear();
    m_emby->searchItems(term, Constants::kTypeMovie, 20, [this, gen](const QJsonArray &items) {
        if (gen != m_searchGeneration) return;
        m_searchMoviesModel->setItems(items);
    });
    m_emby->searchItems(term, Constants::kTypeSeries, 20, [this, gen](const QJsonArray &items) {
        if (gen != m_searchGeneration) return;
        m_searchSeriesModel->setItems(items);
    });
    m_emby->searchPersons(term, 20, [this, gen](const QJsonArray &items) {
        if (gen != m_searchGeneration) return;
        m_searchPeopleModel->setItems(items);
    });
}

void LibraryBrowser::fetchRecommendations() {
    m_recommendModel->clear();
    m_emby->searchItems("", "Movie,Series", 20, [this](const QJsonArray &items) {
        m_recommendModel->setItems(items);
    }, "IsFavoriteOrLiked,Random");
}

void LibraryBrowser::updateCardField(const QString &itemId, const QString &roleName, const QVariant &value) {
    const auto models = m_latestModels.values();
    for (auto *model : models)
        model->updateItemByRoleName(itemId, roleName, value);
    m_contentModel->updateItemByRoleName(itemId, roleName, value);
    m_searchModel->updateItemByRoleName(itemId, roleName, value);
    m_favMoviesModel->updateItemByRoleName(itemId, roleName, value);
    m_favSeriesModel->updateItemByRoleName(itemId, roleName, value);
    m_favEpisodesModel->updateItemByRoleName(itemId, roleName, value);
    m_searchMoviesModel->updateItemByRoleName(itemId, roleName, value);
    m_searchSeriesModel->updateItemByRoleName(itemId, roleName, value);
    m_searchEpisodesModel->updateItemByRoleName(itemId, roleName, value);
    m_recommendModel->updateItemByRoleName(itemId, roleName, value);
    m_suggestionsResumeModel->updateItemByRoleName(itemId, roleName, value);
    m_suggestionsLatestModel->updateItemByRoleName(itemId, roleName, value);
    m_favPeopleModel->updateItemByRoleName(itemId, roleName, value);
    m_searchPeopleModel->updateItemByRoleName(itemId, roleName, value);
    m_cache->updateItemFieldInCache(itemId, roleName, value);
}

void LibraryBrowser::clearAll() {
    m_libraryModel->clear();
    m_contentModel->clear();
    m_searchModel->clear();
    m_genreModel->clear();
    m_studioModel->clear();
    m_suggestionsResumeModel->clear();
    m_suggestionsLatestModel->clear();
    m_favMoviesModel->clear();
    m_favSeriesModel->clear();
    m_favEpisodesModel->clear();
    m_favPeopleModel->clear();
    m_searchMoviesModel->clear();
    m_searchSeriesModel->clear();
    m_searchEpisodesModel->clear();
    m_searchPeopleModel->clear();
    m_recommendModel->clear();
    for (auto *m : m_latestModels)
        delete m;
    m_latestModels.clear();

    ++m_browseGeneration;
    m_loadingMore = false;
    m_paginationLimit = 0;

    bool idChanged = !m_currentLibraryId.isEmpty();
    m_currentLibraryId.clear();
    m_libraryTypes.clear();

    if (m_movieCount != 0) { m_movieCount = 0; emit movieCountChanged(); }
    if (m_seriesCount != 0) { m_seriesCount = 0; emit seriesCountChanged(); }
    if (m_episodeCount != 0) { m_episodeCount = 0; emit episodeCountChanged(); }
    if (idChanged) emit currentLibraryIdChanged();
    emit latestSectionsChanged();
}

void LibraryBrowser::onItemsFetched(const QJsonArray &items, const QString &parentId,
                                    const QString &cacheKey, int totalRecordCount) {
    m_cache->putItems(cacheKey, items);

    // Favorites use direct callbacks (fetchFavorites) — no dispatch here

    // 请求身份 = cacheKey。它含 parentId / includeTypes / filters / sortBy /
    // sortOrder / genreIds / studioIds / personIds —— 正好覆盖 setLibraryTab /
    // setSortBy / setFilter / browseGenre / browseStudio 会改的每一个维度。
    // 原来只比 parentId, 而那几个操作都不改 parentId, 守卫等于没有: 旧排序的
    // 首屏回来照样 setItems, 后面还按新条件 loadMore, 列表变成旧首屏 + 新续页
    if (cacheKey != m_currentFetchKey) return;

    // 续拉批次走 loadMore 的直接回调, 不经过这里 — 到达的只会是首屏/全量数据
    m_contentModel->setItems(items);

    if (m_pendingStudioSwitch) {
        m_pendingStudioSwitch = false;
        m_currentTab = TabDefault;
        emit currentTabChanged();
    }
    if (m_pendingGenreSwitch) {
        m_pendingGenreSwitch = false;
        m_currentTab = TabDefault;
        emit currentTabChanged();
    }

    if (totalRecordCount >= 0) {
        m_totalItems = totalRecordCount;
        emit totalItemsChanged();
    }

    // TabDefault 首批到达后自动渐进拉满 (A-Z 字母索引需要全量数据)
    if (m_currentTab == TabDefault && canLoadMore())
        loadMore();
}

// ── Sort & Filter ──

QString LibraryBrowser::currentSortByString() const {
    // Episodes tab: group by series, then sort within each show by
    // season + episode number. ParentIndexNumber (season) is required —
    // IndexNumber alone ties across seasons (S1E1/S2E1/S1E2 interleave).
    if (m_currentTab == TabEpisodes)
        return QStringLiteral("SeriesSortName,ParentIndexNumber,IndexNumber");
    switch (m_sortBy) {
    case 1: return QStringLiteral("ProductionYear,SortName");
    case 2: return QStringLiteral("CommunityRating,SortName");
    case 3: return QStringLiteral("DateCreated,SortName");
    case 4: return QStringLiteral("DatePlayed,SortName");
    default: return QStringLiteral("SortName");
    }
}

QString LibraryBrowser::buildFiltersString() const {
    QStringList filters;
    if (m_filterFavorites)
        filters << QStringLiteral("IsFavorite");
    if (m_filterPlayed == 1)
        filters << QStringLiteral("IsPlayed");
    else if (m_filterPlayed == 2)
        filters << QStringLiteral("IsUnplayed");
    return filters.join(',');
}

void LibraryBrowser::setSortBy(int v) {
    if (v == m_sortBy) return;
    m_sortBy = v;
    emit sortByChanged();
    applySortAndFilter();
}

void LibraryBrowser::setSortAscending(bool v) {
    if (v == m_sortAscending) return;
    m_sortAscending = v;
    emit sortAscendingChanged();
    applySortAndFilter();
}

void LibraryBrowser::setFilterFavorites(bool v) {
    if (v == m_filterFavorites) return;
    m_filterFavorites = v;
    emit filterFavoritesChanged();
    applySortAndFilter();
}

void LibraryBrowser::setFilterPlayed(int v) {
    if (v == m_filterPlayed) return;
    m_filterPlayed = v;
    emit filterPlayedChanged();
    applySortAndFilter();
}

void LibraryBrowser::applySortAndFilter() {
    if (m_currentLibraryId.isEmpty()) return;
    // Only apply to supported tabs
    if (m_currentTab == TabSuggestions || m_currentTab == TabFavorites
        || m_currentTab == TabGenres || m_currentTab == TabStudios) return;

    ++m_browseGeneration;
    m_loadingMore = false;
    m_paginationLimit = 0;
    m_totalItems = -1;
    emit totalItemsChanged();
    m_contentModel->clear();

    switch (m_currentTab) {
    case TabDefault:
        m_paginationLimit = kPageSize;
        dispatchFetch({
            .parentId = m_currentLibraryId,
            .includeTypes = m_libraryTypes.value(m_currentLibraryId) == "movies" ? Constants::kTypeMovie : Constants::kTypeSeries,
            .filters = buildFiltersString(),
            .sortBy = currentSortByString(),
            .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
            .limit = m_paginationLimit
        });
        break;
    case TabEpisodes:
        m_paginationLimit = kPageSize;
        m_totalItems = -1;
        emit totalItemsChanged();
        dispatchFetch({
            .parentId = m_currentLibraryId,
            .includeTypes = Constants::kTypeEpisode,
            .filters = buildFiltersString(),
            .sortBy = currentSortByString(),
            .sortOrder = m_sortAscending ? QStringLiteral("Ascending") : QStringLiteral("Descending"),
            .limit = m_paginationLimit
        });
        break;
    default:
        break;
    }
}
