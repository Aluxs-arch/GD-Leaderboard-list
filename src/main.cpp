#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------------------
// Layout contract
//
// Every coordinate below is derived from PANEL_W / MARGIN and the measured
// size of the node being placed. Nothing is positioned with free-floating
// magic numbers, so content cannot spill past the panel border: the row
// builder measures its buttons first and gives the level name whatever
// space is left, and the panel height is computed from the row count.
// ---------------------------------------------------------------------------

namespace {
    enum class Source : int { All = 0, Aredl = 1, Classic = 2, Pointercrate = 3 };
    constexpr unsigned AREDL_BIT = 1u << 0;
    constexpr unsigned CLASSIC_BIT = 1u << 1;
    constexpr unsigned POINTERCRATE_BIT = 1u << 2;

    constexpr float PANEL_W = 400.f;
    constexpr float MARGIN = 14.f;
    constexpr float HEADER_H = 30.f;
    constexpr float TABS_H = 22.f;
    constexpr float STATUS_H = 15.f;
    constexpr float FOOTER_H = 26.f;
    constexpr float ROW_GAP = 2.f;
    constexpr float TOP_BLOCK = HEADER_H + 4.f + TABS_H + 5.f + STATUS_H + 5.f;
    constexpr float BOTTOM_BLOCK = 6.f + FOOTER_H + 9.f;

    struct LevelEntry {
        std::string name;
        std::string id; // Geometry Dash's in-game level ID, never a list-internal ID.
        int aredlRank = 0;
        int classicRank = 0;
        int pointercrateRank = 0;
        unsigned sources = 0;
        std::string videoId;
    };

    bool isIndonesian() {
        return Mod::get()->getSettingValue<std::string>("language") == "id";
    }

    std::string tr(char const* english, char const* indonesian) {
        return isIndonesian() ? indonesian : english;
    }

    bool prefBool(char const* key) { return Mod::get()->getSettingValue<bool>(key); }
    int64_t prefInt(char const* key) { return Mod::get()->getSettingValue<int64_t>(key); }
    std::string prefString(char const* key) { return Mod::get()->getSettingValue<std::string>(key); }

    // One palette per theme; the accent is user-configurable.
    struct Palette {
        bool blue = true;
        ccColor4B rowA{}, rowB{};
        ccColor3B text{}, subtext{}, faint{}, accent{}, hairline{};
    };

    Palette currentPalette() {
        Palette p;
        p.blue = prefString("panel-theme") != "midnight";
        auto accent = Mod::get()->getSettingValue<ccColor3B>("accent-color");
        p.accent = accent;
        if (p.blue) {
            p.rowA = ccc4(30, 92, 152, 235);
            p.rowB = ccc4(24, 78, 132, 235);
            p.text = ccColor3B{ 240, 248, 255 };
            p.subtext = ccColor3B{ 176, 208, 236 };
            p.faint = ccColor3B{ 130, 162, 196 };
            p.hairline = ccColor3B{ 19, 62, 106 };
        } else {
            p.rowA = ccc4(28, 38, 53, 235);
            p.rowB = ccc4(22, 30, 43, 235);
            p.text = ccColor3B{ 226, 233, 242 };
            p.subtext = ccColor3B{ 156, 172, 194 };
            p.faint = ccColor3B{ 110, 126, 148 };
            p.hairline = ccColor3B{ 12, 18, 28 };
        }
        return p;
    }

    // Native GD button, scaled to an exact height (and capped in width).
    // Measuring after creation keeps the layout honest on every platform.
    ButtonSprite* makeButton(char const* caption, char const* bg, float height, float maxWidth = 0.f) {
        auto spr = ButtonSprite::create(caption, 0, false, "bigFont.fnt", bg, 0.f, 1.f);
        if (!spr) return nullptr;
        auto size = spr->getContentSize();
        if (size.height <= 0.f || size.width <= 0.f) return spr;
        float scale = height / size.height;
        if (maxWidth > 0.f && size.width * scale > maxWidth) scale = maxWidth / size.width;
        spr->setScale(scale);
        return spr;
    }

    float scaledWidth(CCNode* node) {
        return node ? node->getContentSize().width * node->getScaleX() : 0.f;
    }

    CCLabelBMFont* makeLabel(std::string const& text, char const* font, float scale,
                             ccColor3B color, float maxWidth = 0.f) {
        auto label = CCLabelBMFont::create(text.c_str(), font);
        if (!label) return nullptr;
        label->setScale(scale);
        label->setColor(color);
        if (maxWidth > 0.f) label->limitLabelWidth(maxWidth, scale, scale * .68f);
        return label;
    }

    std::string jsonText(matjson::Value const& object, char const* key) {
        auto value = object[key];
        if (value.isString()) return value.asString().unwrapOr("");
        if (value.isNumber()) return std::to_string(value.asInt().unwrapOr(0));
        return "";
    }

    int jsonInt(matjson::Value const& object, char const* key) {
        auto value = object[key];
        if (value.isNumber()) return value.asInt().unwrapOr(0);
        if (value.isString()) {
            try {
                return std::stoi(value.asString().unwrapOr(""));
            } catch (...) {
                return 0;
            }
        }
        return 0;
    }

    matjson::Value extractList(matjson::Value const& response) {
        if (response.isArray()) return response;
        auto data = response["data"];
        if (data.isArray()) return data;
        for (auto key : { "levels", "results", "items" }) {
            auto direct = response[key];
            if (direct.isArray()) return direct;
            auto nested = data[key];
            if (nested.isArray()) return nested;
        }
        return matjson::Value();
    }

    unsigned sourceBit(Source source) {
        switch (source) {
            case Source::Aredl: return AREDL_BIT;
            case Source::Classic: return CLASSIC_BIT;
            case Source::Pointercrate: return POINTERCRATE_BIT;
            default: return 0;
        }
    }

    char const* sourceName(Source source) {
        switch (source) {
            case Source::Aredl: return "AREDL";
            case Source::Classic: return "CLASSIC";
            case Source::Pointercrate: return "POINTERCRATE";
            default: return "ALL";
        }
    }

    // Only accept YouTube IDs. Never request an arbitrary URL supplied by a list.
    std::string youtubeId(std::string const& url) {
        for (auto marker : { "youtu.be/", "watch?v=", "&v=", "/embed/", "/shorts/" }) {
            auto pos = url.find(marker);
            if (pos == std::string::npos) continue;
            auto id = url.substr(pos + std::char_traits<char>::length(marker), 11);
            if (id.size() == 11 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_';
            })) return id;
        }
        return "";
    }

    // Lists are shared between popup sessions so reopening is instant and
    // does not refetch three APIs every time. Refresh always bypasses this.
    struct ListCache {
        std::vector<LevelEntry> entries;
        std::chrono::steady_clock::time_point stamp;
        size_t failedSources = 0;
        bool valid = false;
    };
    ListCache g_cache;

    web::WebRequest listRequest() {
        auto seconds = std::clamp<int64_t>(prefInt("request-timeout"), 10, 90);
        auto request = web::WebRequest();
        request.timeout(std::chrono::seconds(seconds));
        request.userAgent("Aluxs-Leaderboard-List/2.3 (Geode mod)");
        return request;
    }
}

class LeaderboardPopup : public geode::Popup, public LevelManagerDelegate {
protected:
    // Preferences are snapshotted once so a mid-session settings change can
    // never desynchronize the layout; reopening the panel applies them.
    Palette m_pal;
    size_t m_perPage = 6;
    float m_rowH = 24.f;
    float m_panelH = 0.f;
    bool m_showThumbnails = true;
    bool m_videoPreviews = true;
    bool m_showMarks = true;
    bool m_showLevelId = false;
    bool m_showRank = true;
    bool m_stripes = true;
    bool m_highlightTop = true;
    bool m_descending = false;
    bool m_pageWrap = false;
    bool m_confirmOpen = false;

    Source m_filter = Source::All;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    LoadingSpinner* m_spinner = nullptr;
    CCNode* m_rows = nullptr;
    CCMenu* m_filterMenu = nullptr;
    CCLayerColor* m_tabUnderline = nullptr;
    std::array<CCMenuItemSpriteExtra*, 4> m_filterButtons{};
    CCMenuItemSpriteExtra* m_prevItem = nullptr;
    CCMenuItemSpriteExtra* m_nextItem = nullptr;
    float m_tabW = 0.f;
    float m_statusMaxW = 0.f;
    size_t m_page = 0;
    size_t m_completedRequests = 0;
    size_t m_failedRequests = 0;
    bool m_hasLoaded = false;
    async::TaskHolder<web::WebResponse> m_aredlRequest;
    async::TaskHolder<web::WebResponse> m_classicRequest;
    async::TaskHolder<web::WebResponse> m_pointercrateRequest;
    std::vector<LevelEntry> m_entries;
    std::unordered_map<std::string, size_t> m_entryById;
    std::vector<size_t> m_visibleEntries;
    std::vector<async::TaskHolder<web::WebResponse>> m_previewTasks;
    std::unordered_map<std::string, Ref<CCTexture2D>> m_previewCache;
    int m_openLevelId = 0;
    std::string m_openSearchKey;

    void loadPreferences() {
        m_pal = currentPalette();
        m_perPage = static_cast<size_t>(std::clamp<int64_t>(prefInt("rows-per-page"), 3, 10));
        m_rowH = prefBool("compact-rows") ? 24.f : 30.f;
        m_showThumbnails = prefBool("show-thumbnails");
        m_videoPreviews = prefBool("video-previews");
        m_showMarks = prefBool("show-source-marks");
        m_showLevelId = prefBool("show-level-id");
        m_showRank = prefBool("show-rank");
        m_stripes = prefBool("row-stripes");
        m_highlightTop = prefBool("highlight-top3");
        m_descending = prefBool("sort-descending");
        m_pageWrap = prefBool("page-wrap");
        m_confirmOpen = prefBool("confirm-open");
        m_panelH = TOP_BLOCK + static_cast<float>(m_perPage) * m_rowH
            + static_cast<float>(m_perPage - 1) * ROW_GAP + BOTTOM_BLOCK;

        if (prefBool("remember-tab")) {
            m_filter = static_cast<Source>(std::clamp(Mod::get()->getSavedValue<int>("last-tab", 0), 0, 3));
        } else {
            auto tab = prefString("default-tab");
            m_filter = tab == "aredl" ? Source::Aredl
                : tab == "classic" ? Source::Classic
                : tab == "pointer" ? Source::Pointercrate : Source::All;
        }
    }

    bool setup() {
        auto const W = PANEL_W;
        auto const H = m_panelH;
        auto const innerW = W - 2.f * MARGIN;

        // Popup::init creates the full-screen scrim plus a stock nine-slice.
        // Hide the stock slice; our clean texture is the actual foreground.
        m_bgSprite->setVisible(false);
        auto cover = NineSlice::create(
            (m_pal.blue ? "leaderboard-bg.png"_spr : "leaderboard-bg-midnight.png"_spr),
            {}, { 16.f, 16.f, 16.f, 16.f });
        if (cover) {
            cover->setContentSize({ W, H });
            cover->setPosition({ W / 2.f, H / 2.f });
            cover->setID("leaderboard-cover"_spr);
            m_mainLayer->addChild(cover, -2);
        }

        // Header: close button inside the panel, centered title, optional mark.
        m_closeBtn->setScale(.7f);
        m_closeBtn->setPosition({ 19.f, H - HEADER_H / 2.f });
        setTitle(tr("LEADERBOARD LIST", "PAPAN PERINGKAT"), "goldFont.fnt", .5f, HEADER_H / 2.f);
        if (m_title) m_title->limitLabelWidth(W - 110.f, .5f, .34f);
        if (prefBool("show-header-logo")) {
            auto mark = CCSprite::create("extreme-demon.png"_spr);
            if (mark) {
                mark->setScale(21.f / mark->getContentSize().width);
                mark->setPosition({ W - 26.f, H - HEADER_H / 2.f });
                m_mainLayer->addChild(mark);
            }
        }
        auto headerRule = CCLayerColor::create(ccc4(m_pal.hairline.r, m_pal.hairline.g, m_pal.hairline.b, 190));
        headerRule->setContentSize({ innerW, 1.f });
        headerRule->setPosition({ MARGIN, H - HEADER_H });
        m_mainLayer->addChild(headerRule);

        // Source tabs: four equal slots spanning the inner width.
        m_tabW = (innerW - 3.f * 6.f) / 4.f;
        m_filterMenu = CCMenu::create();
        m_filterMenu->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(m_filterMenu);
        float tabsY = H - HEADER_H - 4.f - TABS_H / 2.f;
        char const* labels[] = { "ALL", "AREDL", "CLASSIC", "POINTER" };
        for (int i = 0; i < 4; ++i) {
            auto item = CCMenuItemSpriteExtra::create(
                makeTabSprite(labels[i], i == static_cast<int>(m_filter)),
                this, menu_selector(LeaderboardPopup::onChooseFilter));
            item->setTag(i);
            item->setPosition({ MARGIN + m_tabW / 2.f + static_cast<float>(i) * (m_tabW + 6.f), tabsY });
            m_filterButtons[static_cast<size_t>(i)] = item;
            m_filterMenu->addChild(item);
        }
        m_tabUnderline = CCLayerColor::create(ccc4(m_pal.accent.r, m_pal.accent.g, m_pal.accent.b, 255));
        m_tabUnderline->setContentSize({ m_tabW - 12.f, 2.f });
        m_mainLayer->addChild(m_tabUnderline);
        updateFilterStyle();

        // Status line with a spinner and a measured refresh button.
        float statusY = H - HEADER_H - 4.f - TABS_H - 5.f - STATUS_H / 2.f;
        auto refresh = CCMenuItemSpriteExtra::create(
            makeButton(tr("REFRESH", "MUAT ULANG").c_str(), "GJ_button_01.png", 18.f, 96.f),
            this, menu_selector(LeaderboardPopup::onRefresh));
        float refreshW = scaledWidth(refresh->getNormalImage());
        auto refreshMenu = CCMenu::create();
        refreshMenu->setPosition({ 0.f, 0.f });
        refresh->setPosition({ W - MARGIN - refreshW / 2.f, statusY });
        refreshMenu->addChild(refresh);
        m_mainLayer->addChild(refreshMenu);

        m_spinner = LoadingSpinner::create(12.f);
        m_spinner->setPosition({ W - MARGIN - refreshW - 13.f, statusY });
        m_spinner->setVisible(false);
        m_mainLayer->addChild(m_spinner);

        m_statusMaxW = innerW - refreshW - 26.f;
        m_status = makeLabel(tr("Preparing live lists…", "Menyiapkan list live…"), "chatFont.fnt",
            .42f, m_pal.text, m_statusMaxW);
        m_status->setAnchorPoint({ 0.f, .5f });
        m_status->setPosition({ MARGIN, statusY });
        m_mainLayer->addChild(m_status);

        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows);

        // Footer: native page arrows around a bounded page counter.
        float rowsBlock = static_cast<float>(m_perPage) * m_rowH + static_cast<float>(m_perPage - 1) * ROW_GAP;
        float footerY = H - TOP_BLOCK - rowsBlock - 6.f - FOOTER_H / 2.f;
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        if (prevSpr && nextSpr) {
            prevSpr->setScale(19.f / prevSpr->getContentSize().height);
            nextSpr->setScale(19.f / nextSpr->getContentSize().height);
            nextSpr->setFlipX(true);
            m_prevItem = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(LeaderboardPopup::onPreviousPage));
            m_nextItem = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(LeaderboardPopup::onNextPage));
            float arrowW = prevSpr->getContentSize().width * prevSpr->getScaleX();
            auto pageMenu = CCMenu::create();
            pageMenu->setPosition({ 0.f, 0.f });
            m_pageLabel = makeLabel("", "chatFont.fnt", .42f, m_pal.text);
            m_pageLabel->setPosition({ W / 2.f, footerY });
            m_mainLayer->addChild(m_pageLabel);
            float gap = 42.f;
            m_prevItem->setPosition({ W / 2.f - gap - arrowW / 2.f, footerY });
            m_nextItem->setPosition({ W / 2.f + gap + arrowW / 2.f, footerY });
            pageMenu->addChild(m_prevItem);
            pageMenu->addChild(m_nextItem);
            m_mainLayer->addChild(pageMenu);
        }
        if (prefBool("show-footer")) {
            auto credit = makeLabel("ALUXS  ·  v2.3.0", "chatFont.fnt", .28f, m_pal.faint);
            credit->setAnchorPoint({ 1.f, .5f });
            credit->setPosition({ W - MARGIN, footerY });
            m_mainLayer->addChild(credit);
        }

        refreshLists(false);
        return true;
    }

    ButtonSprite* makeTabSprite(char const* label, bool selected) {
        auto spr = ButtonSprite::create(label, 160, true, "bigFont.fnt",
            selected ? "GJ_button_01.png" : "GJ_button_05.png", 0.f, 1.f);
        if (spr) {
            auto size = spr->getContentSize();
            if (size.width > 0.f && size.height > 0.f)
                spr->setScale(std::min(TABS_H / size.height, m_tabW / size.width));
        }
        return spr;
    }

    void updateFilterStyle() {
        char const* labels[] = { "ALL", "AREDL", "CLASSIC", "POINTER" };
        for (size_t i = 0; i < m_filterButtons.size(); ++i) {
            if (!m_filterButtons[i]) continue;
            m_filterButtons[i]->setSprite(makeTabSprite(labels[i], i == static_cast<size_t>(m_filter)));
        }
        if (m_tabUnderline) {
            float tabsBottom = m_panelH - HEADER_H - 4.f - TABS_H;
            m_tabUnderline->setPosition({
                MARGIN + 6.f + static_cast<float>(m_filter) * (m_tabW + 6.f), tabsBottom - 3.f });
        }
    }

    void setStatus(std::string const& text, bool loading = false) {
        if (m_status) {
            m_status->setString(text.c_str());
            m_status->limitLabelWidth(m_statusMaxW, .42f, .28f);
        }
        if (m_spinner) m_spinner->setVisible(loading);
    }

    void clearRows() {
        m_previewTasks.clear();
        if (m_rows) m_rows->removeAllChildrenWithCleanup(true);
    }

    int rankFor(LevelEntry const& entry, Source source) const {
        if (source == Source::Aredl) return entry.aredlRank;
        if (source == Source::Classic) return entry.classicRank;
        if (source == Source::Pointercrate) return entry.pointercrateRank;
        if (entry.classicRank) return entry.classicRank;
        if (entry.aredlRank) return entry.aredlRank;
        return entry.pointercrateRank;
    }

    std::string sourceMarks(LevelEntry const& entry) const {
        std::string marks;
        if (entry.sources & AREDL_BIT) marks += "A";
        if (entry.sources & CLASSIC_BIT) marks += marks.empty() ? "C" : "·C";
        if (entry.sources & POINTERCRATE_BIT) marks += marks.empty() ? "P" : "·P";
        return marks;
    }

    bool isInFilter(LevelEntry const& entry, Source source) const {
        return source == Source::All || (entry.sources & sourceBit(source));
    }

    size_t pageCount() const {
        return (m_visibleEntries.size() + m_perPage - 1) / m_perPage;
    }

    void rebuildVisible() {
        m_visibleEntries.clear();
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (isInFilter(m_entries[i], m_filter)) m_visibleEntries.push_back(i);
        }
        std::stable_sort(m_visibleEntries.begin(), m_visibleEntries.end(), [this](size_t a, size_t b) {
            auto rankA = rankFor(m_entries[a], m_filter);
            auto rankB = rankFor(m_entries[b], m_filter);
            if (rankA == 0) return false;
            if (rankB == 0) return true;
            return m_descending ? rankA > rankB : rankA < rankB;
        });
        auto pages = pageCount();
        if (pages && m_page >= pages) m_page = pages - 1;
    }

    bool addToUnifiedList(Source source, matjson::Value const& response) {
        auto list = extractList(response);
        if (!list.isArray()) return false;
        auto bit = sourceBit(source);
        for (size_t i = 0; i < list.size(); ++i) {
            auto item = list[i];
            std::string id = source == Source::Classic ? jsonText(item, "ingame_id") : jsonText(item, "level_id");
            if (id.empty()) id = jsonText(item, "gd_id");
            if (id.empty()) continue;

            size_t index;
            auto found = m_entryById.find(id);
            if (found == m_entryById.end()) {
                index = m_entries.size();
                m_entryById.emplace(id, index);
                LevelEntry entry;
                entry.id = id;
                entry.name = jsonText(item, "name");
                m_entries.push_back(std::move(entry));
            } else {
                index = found->second;
                if (m_entries[index].name.empty()) m_entries[index].name = jsonText(item, "name");
            }

            auto& entry = m_entries[index];
            entry.sources |= bit;
            if (entry.videoId.empty()) {
                auto video = jsonText(item, "video");
                if (video.empty()) video = jsonText(item["video"], "url");
                entry.videoId = youtubeId(video);
            }
            int rank = source == Source::Classic ? jsonInt(item, "placement")
                : jsonInt(item, "position");
            if (!rank) rank = jsonInt(item, "rank");
            if (source == Source::Aredl) entry.aredlRank = rank;
            else if (source == Source::Classic) entry.classicRank = rank;
            else if (source == Source::Pointercrate) entry.pointercrateRank = rank;
        }
        return true;
    }

    void finishSource(bool failed) {
        if (failed) ++m_failedRequests;
        ++m_completedRequests;
        if (m_completedRequests < 3) {
            setStatus(fmt::format("{} {}/3…", tr("Loading live sources", "Memuat sumber live"), m_completedRequests), true);
            return;
        }
        m_hasLoaded = true;
        rebuildVisible();
        renderPage();
        if (m_entries.empty()) {
            setStatus(tr("No list data loaded. Check your connection and retry.",
                "Data list tidak termuat. Periksa koneksi lalu coba lagi."));
        } else {
            if (m_failedRequests == 0) {
                g_cache = { m_entries, std::chrono::steady_clock::now(), m_failedRequests, true };
            }
            setStatus(fmt::format("{} {} · {} {}", m_entries.size(), tr("levels", "level"),
                3 - m_failedRequests, tr("of 3 lists synced", "dari 3 list tersinkron")));
        }
    }

    void onSourceFinished(Source source, web::WebResponse response) {
        if (!response.ok()) {
            finishSource(true);
            return;
        }
        auto parsed = response.json();
        finishSource(!parsed || !addToUnifiedList(source, parsed.unwrap()));
    }

    void fetchPointercratePage(int after) {
        auto url = std::string("https://pointercrate.com/api/v2/demons/listed/?limit=100");
        if (after > 0) url += "&after=" + std::to_string(after);
        m_pointercrateRequest.spawn(listRequest().get(url), [this, after](web::WebResponse response) {
            if (!response.ok()) {
                finishSource(true);
                return;
            }
            auto parsed = response.json();
            if (!parsed) {
                finishSource(true);
                return;
            }
            auto data = parsed.unwrap();
            auto list = extractList(data);
            if (!list.isArray() || !addToUnifiedList(Source::Pointercrate, data)) {
                finishSource(true);
                return;
            }
            int lastPosition = list.size() ? jsonInt(list[list.size() - 1], "position") : 0;
            if (list.size() == 100 && lastPosition > after) {
                fetchPointercratePage(lastPosition);
                return;
            }
            finishSource(false);
        });
    }

    void adoptCache() {
        m_entries = g_cache.entries;
        m_entryById.clear();
        for (size_t i = 0; i < m_entries.size(); ++i) m_entryById.emplace(m_entries[i].id, i);
        m_hasLoaded = true;
        m_page = 0;
        rebuildVisible();
        renderPage();
        auto age = std::chrono::duration_cast<std::chrono::minutes>(
            std::chrono::steady_clock::now() - g_cache.stamp).count();
        setStatus(fmt::format("{} {} · {} {}m", m_entries.size(), tr("levels", "level"),
            tr("cached", "cache"), age));
    }

    void refreshLists(bool force) {
        auto cacheMinutes = std::clamp<int64_t>(prefInt("cache-minutes"), 0, 60);
        if (!force && cacheMinutes > 0 && g_cache.valid) {
            auto age = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - g_cache.stamp).count();
            if (age < cacheMinutes) {
                adoptCache();
                return;
            }
        }
        m_aredlRequest.cancel();
        m_classicRequest.cancel();
        m_pointercrateRequest.cancel();
        m_hasLoaded = false;
        m_completedRequests = 0;
        m_failedRequests = 0;
        m_entries.clear();
        m_previewCache.clear();
        m_entryById.clear();
        m_visibleEntries.clear();
        m_page = 0;
        clearRows();
        renderPage();
        setStatus(tr("Syncing AREDL, Classic and Pointercrate…", "Menyinkronkan AREDL, Classic, dan Pointercrate…"), true);

        m_aredlRequest.spawn(listRequest().get("https://api.aredl.net/api/aredl/levels"),
            [this](web::WebResponse response) { onSourceFinished(Source::Aredl, std::move(response)); });
        m_classicRequest.spawn(listRequest().get("https://api.demonlist.org/level/classic/list"),
            [this](web::WebResponse response) { onSourceFinished(Source::Classic, std::move(response)); });
        fetchPointercratePage(0);
    }

    void attachPreview(CCNode* holder, CCTexture2D* texture) {
        if (!holder || !holder->getParent() || !texture) return;
        holder->removeAllChildrenWithCleanup(true);
        auto sprite = CCSprite::createWithTexture(texture);
        if (!sprite) return;
        auto imageSize = sprite->getContentSize();
        auto window = holder->getContentSize();
        if (imageSize.width <= 0 || imageSize.height <= 0) return;
        sprite->setScale(std::min(window.width / imageSize.width, window.height / imageSize.height));
        sprite->setPosition({ window.width / 2.f, window.height / 2.f });
        holder->addChild(sprite);
    }

    void loadPreview(CCNode* holder, std::string const& videoId) {
        auto cached = m_previewCache.find(videoId);
        if (cached != m_previewCache.end()) {
            attachPreview(holder, cached->second.data());
            return;
        }
        // Only visible rows request images. TaskHolder cancels old-page work;
        // the retained holder cannot outlive its row after the callback.
        m_previewTasks.emplace_back();
        m_previewTasks.back().spawn(
            web::WebRequest().timeout(std::chrono::seconds(8)).get(
                "https://i.ytimg.com/vi/" + videoId + "/mqdefault.jpg"),
            [this, holder = Ref<CCNode>(holder), videoId](web::WebResponse response) {
                if (!response.ok() || !holder->getParent() || response.data().size() > 512 * 1024 || response.data().empty()) return;
                auto bytes = response.data();
                auto image = new CCImage();
                bool decoded = image->initWithImageData(bytes.data(), static_cast<int>(bytes.size()), CCImage::kFmtJpg);
                if (!decoded || image->getWidth() > 1024 || image->getHeight() > 1024) {
                    image->release();
                    return;
                }
                auto texture = new CCTexture2D();
                bool uploaded = texture->initWithImage(image);
                image->release();
                if (!uploaded) { texture->release(); return; }
                texture->autorelease();
                if (m_previewCache.size() >= 32) m_previewCache.clear();
                m_previewCache.emplace(videoId, texture);
                attachPreview(holder.data(), texture);
            }
        );
    }

    void renderPage() {
        clearRows();
        auto const innerW = PANEL_W - 2.f * MARGIN;
        float rowsTop = m_panelH - TOP_BLOCK;
        float buttonH = m_rowH - 7.f;

        // Measure the action buttons once per page; every row reuses the
        // result, and the name column gets exactly the space that remains.
        auto openLabel = tr("OPEN", "BUKA");
        float openW = scaledWidth(makeButton(openLabel.c_str(), "GJ_button_02.png", buttonH, 58.f));
        float idW = scaledWidth(makeButton("ID", "GJ_button_01.png", buttonH, 34.f));
        float infoW = scaledWidth(makeButton("INFO", "GJ_button_01.png", buttonH, 50.f));
        float actionsW = openW + idW + infoW + 10.f + 6.f;
        float marksW = m_showMarks ? 30.f : 0.f;
        float thumbH = m_rowH - 6.f;
        float thumbW = thumbH * (16.f / 9.f);
        float leftW = 7.f + (m_showRank ? 34.f : 0.f) + (m_showThumbnails ? thumbW + 7.f : 0.f);
        float nameMax = std::max(40.f, innerW - actionsW - marksW - leftW - 6.f);

        auto begin = m_page * m_perPage;
        auto end = std::min(begin + m_perPage, m_visibleEntries.size());
        for (size_t slot = 0; begin + slot < end; ++slot) {
            auto const& entry = m_entries[m_visibleEntries[begin + slot]];
            auto rowColor = (m_stripes && slot % 2) ? m_pal.rowB : m_pal.rowA;
            auto row = CCLayerColor::create(rowColor);
            row->setContentSize({ innerW, m_rowH });
            row->setPosition({ MARGIN, rowsTop - static_cast<float>(slot + 1) * m_rowH - static_cast<float>(slot) * ROW_GAP });
            row->setID("leaderboard-level-row"_spr);
            m_rows->addChild(row);
            auto rail = CCLayerColor::create(ccc4(m_pal.accent.r, m_pal.accent.g, m_pal.accent.b, 255));
            rail->setContentSize({ 2.f, m_rowH });
            row->addChild(rail);

            float midY = m_rowH / 2.f;
            float cx = 7.f;
            if (m_showRank) {
                auto rankNumber = rankFor(entry, m_filter);
                auto rank = makeLabel(rankNumber ? fmt::format("#{}", rankNumber) : "–",
                    "goldFont.fnt", .3f, ccColor3B{ 255, 255, 255 }, 30.f);
                if (rank) {
                    if (m_highlightTop && rankNumber > 0 && rankNumber <= 3) {
                        rank->setColor(rankNumber == 1 ? ccColor3B{ 255, 216, 96 }
                            : rankNumber == 2 ? ccColor3B{ 214, 226, 238 } : ccColor3B{ 232, 168, 118 });
                    } else {
                        rank->setColor(m_pal.subtext);
                    }
                    rank->setAnchorPoint({ 0.f, .5f });
                    rank->setPosition({ cx, midY });
                    row->addChild(rank);
                }
                cx += 34.f;
            }
            if (m_showThumbnails) {
                auto holder = CCNode::create();
                holder->setContentSize({ thumbW, thumbH });
                holder->setPosition({ cx, (m_rowH - thumbH) / 2.f });
                row->addChild(holder);
                // Neutral placeholder: an honest icon, never fake "art".
                auto tile = CCLayerColor::create(m_pal.blue ? ccc4(16, 52, 90, 255) : ccc4(12, 18, 28, 255));
                tile->setContentSize({ thumbW, thumbH });
                holder->addChild(tile);
                auto icon = CCSprite::create("extreme-demon.png"_spr);
                if (icon) {
                    icon->setScale((thumbH * .72f) / icon->getContentSize().height);
                    icon->setPosition({ thumbW / 2.f, thumbH / 2.f });
                    icon->setOpacity(70);
                    holder->addChild(icon);
                }
                if (m_videoPreviews && !entry.videoId.empty()) loadPreview(holder, entry.videoId);
                cx += thumbW + 7.f;
            }

            float nameY = m_showLevelId ? midY + 4.5f : midY;
            auto name = makeLabel(entry.name.empty() ? tr("Unknown level", "Level tanpa nama") : entry.name,
                "chatFont.fnt", m_rowH < 27.f ? .4f : .44f, m_pal.text, nameMax);
            if (name) {
                name->setAnchorPoint({ 0.f, .5f });
                name->setPosition({ cx, nameY });
                row->addChild(name);
            }
            if (m_showLevelId) {
                auto idLabel = makeLabel("ID " + entry.id, "chatFont.fnt", .28f, m_pal.faint, nameMax);
                if (idLabel) {
                    idLabel->setAnchorPoint({ 0.f, .5f });
                    idLabel->setPosition({ cx, midY - 5.5f });
                    row->addChild(idLabel);
                }
            }

            // Actions are laid out right-to-left from measured widths.
            auto menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            row->addChild(menu);
            int tag = static_cast<int>(m_visibleEntries[begin + slot]);
            float x = innerW - 6.f;
            auto open = CCMenuItemSpriteExtra::create(makeButton(openLabel.c_str(), "GJ_button_02.png", buttonH, 58.f),
                this, menu_selector(LeaderboardPopup::onOpenEntry));
            open->setTag(tag);
            x -= openW / 2.f;
            open->setPosition({ x, midY });
            menu->addChild(open);
            x -= openW / 2.f + 5.f;
            auto copy = CCMenuItemSpriteExtra::create(makeButton("ID", "GJ_button_01.png", buttonH, 34.f),
                this, menu_selector(LeaderboardPopup::onCopyID));
            copy->setTag(tag);
            x -= idW / 2.f;
            copy->setPosition({ x, midY });
            menu->addChild(copy);
            x -= idW / 2.f + 5.f;
            auto info = CCMenuItemSpriteExtra::create(makeButton("INFO", "GJ_button_01.png", buttonH, 50.f),
                this, menu_selector(LeaderboardPopup::onShowDetails));
            info->setTag(tag);
            x -= infoW / 2.f;
            info->setPosition({ x, midY });
            menu->addChild(info);
            x -= infoW / 2.f + 8.f;

            if (m_showMarks) {
                auto badge = makeLabel(sourceMarks(entry), "chatFont.fnt", .3f, m_pal.subtext, 26.f);
                if (badge) {
                    badge->setAnchorPoint({ 1.f, .5f });
                    badge->setPosition({ x, midY });
                    row->addChild(badge);
                }
            }
        }
        updatePageControls();
    }

    void updatePageControls() {
        auto pages = pageCount();
        if (m_pageLabel) {
            m_pageLabel->setString(pages ? fmt::format("{} / {}", m_page + 1, pages).c_str() : "0 / 0");
            m_pageLabel->limitLabelWidth(64.f, .42f, .3f);
        }
        bool wrap = m_pageWrap && pages > 1;
        if (m_prevItem) {
            m_prevItem->setVisible(pages > 1);
            m_prevItem->setEnabled(wrap || m_page > 0);
            m_prevItem->setOpacity(m_prevItem->isEnabled() ? 255 : 90);
        }
        if (m_nextItem) {
            m_nextItem->setVisible(pages > 1);
            m_nextItem->setEnabled(wrap || m_page + 1 < pages);
            m_nextItem->setOpacity(m_nextItem->isEnabled() ? 255 : 90);
        }
    }

    void showSelectedLevel(LevelEntry const& entry) {
        auto rankText = [](int rank) { return rank ? fmt::format("#{}", rank) : std::string("—"); };
        auto detail = fmt::format("{}\nID: {}\nAREDL: {}   Classic: {}   Pointercrate: {}\n{}: {}",
            entry.name, entry.id, rankText(entry.aredlRank), rankText(entry.classicRank),
            rankText(entry.pointercrateRank), tr("Sources", "Sumber"), sourceMarks(entry));
        FLAlertLayer::create(tr("LEVEL DETAILS", "DETAIL LEVEL").c_str(), detail.c_str(), "OK")->show();
    }

public:
    static LeaderboardPopup* create() {
        auto ret = new LeaderboardPopup();
        ret->loadPreferences();
        if (ret->init(PANEL_W, ret->m_panelH) && ret->setup()) {
            // Fit the foreground to the viewport; the scrim stays full-screen.
            auto visible = CCDirector::sharedDirector()->getWinSize();
            auto preference = prefString("popup-size");
            float sizeFactor = preference == "large" ? 1.f : preference == "normal" ? .88f : .75f;
            auto fit = std::min({ 1.f, (visible.width - 16.f) / PANEL_W,
                (visible.height - 16.f) / ret->m_panelH });
            ret->m_mainLayer->setScale(fit * sizeFactor);
            auto dim = prefBool("dim-background")
                ? static_cast<GLubyte>(std::clamp<int64_t>(prefInt("dim-strength"), 0, 255)) : 0;
            ret->setOpacity(dim);
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void onChooseFilter(CCObject* sender) {
        m_filter = static_cast<Source>(sender->getTag());
        if (prefBool("remember-tab"))
            Mod::get()->setSavedValue("last-tab", static_cast<int>(m_filter));
        m_page = 0;
        updateFilterStyle();
        if (!m_hasLoaded) return;
        rebuildVisible();
        renderPage();
        setStatus(fmt::format("{} · {} {}", sourceName(m_filter), m_visibleEntries.size(), tr("levels", "level")));
    }

    void onRefresh(CCObject*) { refreshLists(true); }

    void onPreviousPage(CCObject*) {
        auto pages = pageCount();
        if (!pages) return;
        if (m_page) --m_page;
        else if (m_pageWrap) m_page = pages - 1;
        else return;
        renderPage();
    }

    void onNextPage(CCObject*) {
        auto pages = pageCount();
        if (!pages) return;
        if (m_page + 1 < pages) ++m_page;
        else if (m_pageWrap) m_page = 0;
        else return;
        renderPage();
    }

    void onShowDetails(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
        if (index < m_entries.size()) showSelectedLevel(m_entries[index]);
    }

    void onCopyID(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
        if (index >= m_entries.size()) return;
        auto result = utils::clipboard::write(m_entries[index].id);
        setStatus(result ? fmt::format("{} {}", tr("Copied ID", "ID disalin"), m_entries[index].id)
                         : tr("Clipboard copy failed.", "Gagal menyalin ke clipboard."));
    }

    ~LeaderboardPopup() override {
        auto manager = GameLevelManager::sharedState();
        if (manager->m_levelManagerDelegate == this) manager->m_levelManagerDelegate = nullptr;
    }

    void finishOpenRequest(char const* message) {
        if (GameLevelManager::sharedState()->m_levelManagerDelegate == this)
            GameLevelManager::sharedState()->m_levelManagerDelegate = nullptr;
        m_openSearchKey.clear();
        m_openLevelId = 0;
        if (message) setStatus(message);
    }

    void showNativeLevel(GJGameLevel* level) {
        finishOpenRequest(nullptr);
        auto scene = LevelInfoLayer::scene(level, false);
        if (scene) CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(.25f, scene));
        else setStatus(tr("Could not open this level.", "Tidak dapat membuka level ini."));
    }

    void loadLevelsFinished(CCArray* levels, char const* key) override {
        if (!key || m_openSearchKey != key) return;
        if (levels) {
            for (auto level : CCArrayExt<GJGameLevel*>(levels)) {
                if (level && level->m_levelID.value() == m_openLevelId) {
                    showNativeLevel(level);
                    return;
                }
            }
        }
        finishOpenRequest(tr("Level not found on GD servers. Try again later.",
            "Level tidak ditemukan di server GD. Coba lagi nanti.").c_str());
    }

    void loadLevelsFailed(char const* key) override {
        if (key && m_openSearchKey == key)
            finishOpenRequest(tr("GD server unavailable. Try again later.",
                "Server GD tidak tersedia. Coba lagi nanti.").c_str());
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int) override {
        loadLevelsFinished(levels, key);
    }

    void loadLevelsFailed(char const* key, int) override {
        loadLevelsFailed(key);
    }

    void openEntry(size_t index) {
        if (index >= m_entries.size()) return;
        auto const& entry = m_entries[index];
        int id = 0;
        auto [end, error] = std::from_chars(entry.id.data(), entry.id.data() + entry.id.size(), id);
        if (error != std::errc{} || end != entry.id.data() + entry.id.size() || id <= 0) {
            setStatus(tr("Invalid in-game level ID.", "ID level dalam game tidak valid."));
            return;
        }
        auto manager = GameLevelManager::sharedState();
        if (auto saved = manager->getSavedLevel(id)) {
            showNativeLevel(saved);
            return;
        }
        if (manager->m_levelManagerDelegate && manager->m_levelManagerDelegate != this) {
            setStatus(tr("GD is busy loading another list. Try again.",
                "GD sedang memuat daftar lain. Coba lagi."));
            return;
        }
        auto search = GJSearchObject::create(SearchType::Search, entry.id);
        if (!search) return;
        m_openLevelId = id;
        m_openSearchKey = search->getKey();
        manager->m_levelManagerDelegate = this;
        setStatus(tr("Opening level in Geometry Dash...", "Membuka level di Geometry Dash..."));
        manager->getOnlineLevels(search);
    }

    void onOpenEntry(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
        if (index >= m_entries.size()) return;
        if (m_confirmOpen) {
            auto const& entry = m_entries[index];
            createQuickPopup(tr("OPEN LEVEL", "BUKA LEVEL").c_str(),
                fmt::format("{}\nID: {}", entry.name, entry.id),
                tr("Cancel", "Batal").c_str(), tr("Open", "Buka").c_str(),
                [self = Ref<LeaderboardPopup>(this), index](FLAlertLayer*, bool confirmed) {
                    if (confirmed && self->getParent()) self->openEntry(index);
                });
            return;
        }
        openEntry(index);
    }
};

// A menu owns the touch, so taps still use the normal GD menu-item callback.
// Once the finger moves past a short threshold it becomes a drag instead.
class DraggableFabMenu : public CCMenu {
    CCPoint m_touchStart = {};
    CCPoint m_origin = {};
    float m_edge = 45.f;
    bool m_dragged = false;

    CCPoint clamped(CCPoint point) {
        auto parentSize = getParent()->getContentSize();
        return { std::clamp(point.x, m_edge, std::max(m_edge, parentSize.width - m_edge)),
            std::clamp(point.y, m_edge, std::max(m_edge, parentSize.height - m_edge)) };
    }

public:
    static DraggableFabMenu* create(float edge) {
        auto menu = new DraggableFabMenu();
        if (menu && menu->init()) {
            menu->m_edge = edge;
            menu->autorelease();
            return menu;
        }
        delete menu;
        return nullptr;
    }

    void restorePosition() {
        auto bounds = getParent()->getContentSize();
        auto x = std::clamp(Mod::get()->getSavedValue<float>("floating-x", .89f), 0.f, 1.f);
        auto y = std::clamp(Mod::get()->getSavedValue<float>("floating-y", .35f), 0.f, 1.f);
        setPosition(clamped({ bounds.width * x, bounds.height * y }));
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        if (!CCMenu::ccTouchBegan(touch, event)) return false;
        m_touchStart = getParent()->convertToNodeSpace(touch->getLocation());
        m_origin = getPosition();
        m_dragged = false;
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) override {
        auto local = getParent()->convertToNodeSpace(touch->getLocation());
        auto dx = local.x - m_touchStart.x;
        auto dy = local.y - m_touchStart.y;
        if (!m_dragged && dx * dx + dy * dy > 36.f) {
            m_dragged = true;
            CCMenu::ccTouchCancelled(touch, event);
        }
        if (m_dragged) setPosition(clamped({ m_origin.x + dx, m_origin.y + dy }));
        else CCMenu::ccTouchMoved(touch, event);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) override {
        if (!m_dragged) {
            CCMenu::ccTouchEnded(touch, event);
            return;
        }
        auto bounds = getParent()->getContentSize();
        if (bounds.width > 0.f && bounds.height > 0.f) {
            Mod::get()->setSavedValue("floating-x", getPositionX() / bounds.width);
            Mod::get()->setSavedValue("floating-y", getPositionY() / bounds.height);
        }
        m_dragged = false;
    }

    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override {
        if (!m_dragged) CCMenu::ccTouchCancelled(touch, event);
        m_dragged = false;
    }
};

namespace {
    // Retained so settings changes can rebuild the shortcut while the menu
    // is on screen; getParent() tells us whether it is still attached.
    Ref<DraggableFabMenu> g_fab;

    void buildFloatingButton(MenuLayer* layer);
}

class $modify(LeaderboardMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        buildFloatingButton(this);
        return true;
    }

    void onLeaderboard(CCObject*) {
        if (auto popup = LeaderboardPopup::create()) popup->show();
        else log::error("Leaderboard popup creation failed");
    }
};

namespace {
    void buildFloatingButton(MenuLayer* layer) {
        if (!layer) return;
        if (g_fab) {
            g_fab->removeFromParentAndCleanup(true);
            g_fab = nullptr;
        }
        if (!prefBool("show-floating-button")) return;

        auto icon = CCSprite::create("floating-icon.png"_spr);
        if (!icon) return;
        auto preference = prefString("floating-size");
        float diameter = preference == "very-small" ? 24.f : preference == "medium" ? 44.f
            : preference == "large" ? 58.f : 32.f;
        icon->setScale(diameter / icon->getContentSize().width);
        auto opacity = static_cast<GLubyte>(std::clamp<int64_t>(prefInt("floating-opacity"), 30, 100) * 255 / 100);
        icon->setOpacity(opacity);
        bool showLabel = prefBool("show-floating-label");
        auto menu = DraggableFabMenu::create(diameter / 2.f + (showLabel ? 20.f : 9.f));
        if (!menu) return;
        menu->setID("aluxs-leaderboard-fab"_spr);
        auto button = CCMenuItemSpriteExtra::create(icon, layer, menu_selector(LeaderboardMenuLayer::onLeaderboard));
        menu->addChild(button);
        if (showLabel) {
            auto label = CCLabelBMFont::create(tr("LIST", "DAFTAR").c_str(), "goldFont.fnt");
            label->setScale(0.32f);
            label->setPosition({ 0.f, -diameter / 2.f - 9.f });
            label->setOpacity(opacity);
            menu->addChild(label);
        }
        if (!prefBool("reduce-motion")) {
            button->runAction(CCRepeatForever::create(CCSequence::create(
                CCScaleTo::create(1.6f, 1.035f),
                CCScaleTo::create(1.6f, 1.0f), nullptr
            )));
        }
        layer->addChild(menu, 100);
        menu->restorePosition();
        g_fab = menu;
    }
}

$on_mod(Loaded) {
    // "Reset position" button on the settings page.
    ButtonSettingPressedEventV3(Mod::get(), "reset-floating-position").listen([](std::string_view) {
        Mod::get()->setSavedValue("floating-x", .89f);
        Mod::get()->setSavedValue("floating-y", .35f);
        if (g_fab && g_fab->getParent()) g_fab->restorePosition();
        Notification::create(tr("Shortcut position reset", "Posisi pintasan diatur ulang"),
            NotificationIcon::Success)->show();
    }).leak();

    // Shortcut tweaks apply live while the main menu is on screen.
    for (auto key : { "show-floating-button", "floating-size", "show-floating-label",
                      "floating-opacity", "reduce-motion", "language" }) {
        SettingChangedEventV3(Mod::get(), key).listen([](std::shared_ptr<SettingV3>) {
            if (g_fab && g_fab->getParent()) {
                buildFloatingButton(static_cast<MenuLayer*>(g_fab->getParent()));
            }
        }).leak();
    }
}
