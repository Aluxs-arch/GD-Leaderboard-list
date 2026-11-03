#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

namespace {
    // “All” is a merged view. The other tabs are filters over that same cache.
    enum class Source : int { All = 0, Aredl = 1, Classic = 2, Pointercrate = 3 };
    constexpr unsigned AREDL_BIT = 1u << 0;
    constexpr unsigned CLASSIC_BIT = 1u << 1;
    constexpr unsigned POINTERCRATE_BIT = 1u << 2;
    constexpr size_t LEVELS_PER_PAGE = 8;

    struct LevelEntry {
        std::string name;
        std::string id; // Geometry Dash's in-game level ID, never a list-internal ID.
        int aredlRank = 0;
        int classicRank = 0;
        int pointercrateRank = 0;
        unsigned sources = 0;
    };

    bool isIndonesian() {
        return Mod::get()->getSettingValue<std::string>("language") == "id";
    }

    std::string tr(char const* english, char const* indonesian) {
        return isIndonesian() ? indonesian : english;
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

    std::string urlFor(Source source, std::string const& id, int rank) {
        switch (source) {
            case Source::Aredl: return "https://aredl.net/list/" + id;
            case Source::Classic: return "https://demonlist.org/classic/" + std::to_string(rank);
            case Source::Pointercrate: return "https://pointercrate.com/demonlist/" + std::to_string(rank);
            default: return "https://aredl.net/list/" + id;
        }
    }

    void openURL(std::string const& url) {
        if (!url.empty()) utils::web::openLinkInBrowser(url);
    }
}

class LeaderboardPopup : public geode::Popup {
protected:
    Source m_filter = Source::All;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCNode* m_rows = nullptr;
    CCMenu* m_pageMenu = nullptr;
    CCMenu* m_filterMenu = nullptr;
    std::array<CCMenuItemSpriteExtra*, 4> m_filterButtons{};
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

    bool setup() {
        setTitle(tr("LEADERBOARD LIST", "DAFTAR LEADERBOARD"), "goldFont.fnt", 0.72f);
        auto size = m_mainLayer->getContentSize();

        // An opaque canvas masks the game's default brown popup skin. Keep the
        // artwork subtle so labels stay readable over bright game backgrounds.
        auto canvas = CCLayerColor::create(ccc4(12, 21, 37, 255));
        canvas->setContentSize(size);
        m_mainLayer->addChild(canvas, -3);

        auto background = CCSprite::create("leaderboard-bg.png"_spr);
        if (background) {
            background->setAnchorPoint({ 0.f, 0.f });
            background->setPosition({ 15.f, 12.f });
            background->setScaleX(410.f / background->getContentSize().width);
            background->setScaleY(306.f / background->getContentSize().height);
            background->setOpacity(72);
            background->setID("leaderboard-panel-background"_spr);
            m_mainLayer->addChild(background, -2);
        }

        auto panel = CCLayerColor::create(ccc4(10, 17, 31, 80));
        panel->setContentSize({ 410.f, 306.f });
        panel->setPosition({ 15.f, 12.f });
        m_mainLayer->addChild(panel, -1);

        auto accent = CCLayerColor::create(ccc4(224, 178, 91, 255));
        accent->setContentSize({ 342.f, 1.f });
        accent->setPosition({ 73.f, size.height - 73.f });
        m_mainLayer->addChild(accent);

        auto logo = CCSprite::create("leaderboard-logo.png"_spr);
        if (logo) {
            logo->setScale(42.f / logo->getContentSize().width);
            logo->setPosition({ 43.f, size.height - 49.f });
            logo->setID("leaderboard-brand-mark"_spr);
            m_mainLayer->addChild(logo);
        }

        auto brand = CCLabelBMFont::create("ALUXS  /  EXTREME DEMON HUB", "goldFont.fnt");
        brand->setScale(0.42f);
        brand->setAnchorPoint({ 0.f, 0.5f });
        brand->setPosition({ 73.f, size.height - 39.f });
        m_mainLayer->addChild(brand);

        auto subtitle = CCLabelBMFont::create(
            tr("One live index · three communities · one tap IDs", "Satu indeks live · tiga komunitas · salin ID sekali tap").c_str(),
            "chatFont.fnt"
        );
        subtitle->setScale(0.50f);
        subtitle->setAnchorPoint({ 0.f, 0.5f });
        subtitle->setColor({ 160, 181, 207 });
        subtitle->setPosition({ 73.f, size.height - 59.f });
        m_mainLayer->addChild(subtitle);

        m_filterMenu = CCMenu::create();
        m_filterMenu->setPosition({ size.width / 2.f, size.height - 91.f });
        m_mainLayer->addChild(m_filterMenu);
        addFilterButton("ALL", Source::All, -145.f);
        addFilterButton("AREDL", Source::Aredl, -48.f);
        addFilterButton("CLASSIC", Source::Classic, 48.f);
        addFilterButton("POINTER", Source::Pointercrate, 145.f);

        m_status = CCLabelBMFont::create("Preparing live lists…", "chatFont.fnt");
        m_status->setScale(0.56f);
        m_status->setColor({ 165, 190, 218 });
        m_status->setAnchorPoint({ 0.f, 0.5f });
        m_status->setPosition({ 25.f, size.height - 118.f });
        m_status->setWidth(310.f);
        m_mainLayer->addChild(m_status);

        auto refreshMenu = CCMenu::create();
        refreshMenu->setPosition({ size.width - 53.f, size.height - 118.f });
        refreshMenu->addChild(CCMenuItemSpriteExtra::create(
            ButtonSprite::create(tr("REFRESH", "MUAT ULANG").c_str(), 76, true, "goldFont.fnt", "GJ_button_01.png", 26.f, 0.60f),
            this, menu_selector(LeaderboardPopup::onRefresh)
        ));
        m_mainLayer->addChild(refreshMenu);

        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows);

        m_pageMenu = CCMenu::create();
        m_pageMenu->setPosition({ size.width / 2.f, 31.f });
        m_pageMenu->setVisible(false);
        m_pageMenu->addChild(CCMenuItemSpriteExtra::create(
            ButtonSprite::create("<", 34, true, "goldFont.fnt", "GJ_button_05.png", 24.f, 0.58f),
            this, menu_selector(LeaderboardPopup::onPreviousPage)
        ));
        auto next = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(">", 34, true, "goldFont.fnt", "GJ_button_05.png", 24.f, 0.58f),
            this, menu_selector(LeaderboardPopup::onNextPage)
        );
        next->setPosition({ 86.f, 0.f });
        m_pageMenu->addChild(next);
        m_mainLayer->addChild(m_pageMenu);

        m_pageLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_pageLabel->setScale(0.48f);
        m_pageLabel->setColor({ 182, 198, 220 });
        m_pageLabel->setPosition({ size.width / 2.f + 43.f, 31.f });
        m_mainLayer->addChild(m_pageLabel);

        auto footer = CCLabelBMFont::create(tr("ALUXS  ·  DRAG THE SHORTCUT TO MOVE", "ALUXS  ·  GESER TOMBOL UNTUK MEMINDAHKAN"), "chatFont.fnt");
        footer->setScale(0.40f);
        footer->setColor({ 109, 128, 154 });
        footer->setPosition({ size.width / 2.f, 11.f });
        m_mainLayer->addChild(footer);

        refreshLists();
        return true;
    }

    void addFilterButton(char const* label, Source source, float x) {
        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(label, 78, true, "goldFont.fnt", "GJ_button_05.png", 28.f, 0.60f),
            this, menu_selector(LeaderboardPopup::onChooseFilter)
        );
        button->setTag(static_cast<int>(source));
        button->setPosition({ x, 0.f });
        m_filterButtons[static_cast<size_t>(source)] = button;
        m_filterMenu->addChild(button);
        updateFilterStyle();
    }

    void updateFilterStyle() {
        for (size_t i = 0; i < m_filterButtons.size(); ++i) {
            if (m_filterButtons[i]) m_filterButtons[i]->setOpacity(i == static_cast<size_t>(m_filter) ? 255 : 175);
        }
    }

    void setStatus(std::string const& text) {
        if (m_status) m_status->setString(text.c_str());
    }

    void clearRows() {
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
        if (entry.sources & CLASSIC_BIT) marks += marks.empty() ? "C" : " · C";
        if (entry.sources & POINTERCRATE_BIT) marks += marks.empty() ? "P" : " · P";
        return marks;
    }

    bool isInFilter(LevelEntry const& entry, Source source) const {
        return source == Source::All || (entry.sources & sourceBit(source));
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
            return rankA < rankB;
        });
        auto pages = (m_visibleEntries.size() + LEVELS_PER_PAGE - 1) / LEVELS_PER_PAGE;
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
            setStatus(fmt::format("{} {}/3…", tr("Loading live sources", "Memuat sumber live"), m_completedRequests));
            return;
        }
        m_hasLoaded = true;
        rebuildVisible();
        renderPage();
        if (m_entries.empty()) {
            setStatus(tr("No list data loaded. Check your connection and retry.", "Data list tidak termuat. Periksa koneksi lalu coba lagi."));
        } else if (m_failedRequests) {
            setStatus(fmt::format("{} {} · {} {}", m_entries.size(), tr("unique levels", "level unik"), m_failedRequests, tr("source(s) unavailable", "sumber gagal dimuat")));
        } else {
            setStatus(fmt::format("{} {} · {}", m_entries.size(), tr("unique levels", "level unik"), tr("all 3 lists synced", "3 list tersinkron")));
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
        auto request = web::WebRequest();
        request.timeout(std::chrono::seconds(30));
        request.userAgent("Aluxs-Leaderboard-List/1.1 (Geode mod)");
        auto url = std::string("https://pointercrate.com/api/v2/demons/listed/?limit=100");
        if (after > 0) url += "&after=" + std::to_string(after);
        m_pointercrateRequest.spawn(request.get(url), [this, after](web::WebResponse response) {
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

    void refreshLists() {
        m_aredlRequest.cancel();
        m_classicRequest.cancel();
        m_pointercrateRequest.cancel();
        m_hasLoaded = false;
        m_completedRequests = 0;
        m_failedRequests = 0;
        m_entries.clear();
        m_entryById.clear();
        m_visibleEntries.clear();
        m_page = 0;
        clearRows();
        setStatus(tr("Syncing AREDL, Classic and Pointercrate…", "Menyinkronkan AREDL, Classic, dan Pointercrate…"));

        auto request = web::WebRequest();
        request.timeout(std::chrono::seconds(30));
        request.userAgent("Aluxs-Leaderboard-List/1.1 (Geode mod)");
        m_aredlRequest.spawn(request.get("https://api.aredl.net/api/aredl/levels"), [this](web::WebResponse response) {
            onSourceFinished(Source::Aredl, std::move(response));
        });
        auto classicRequest = web::WebRequest();
        classicRequest.timeout(std::chrono::seconds(30));
        classicRequest.userAgent("Aluxs-Leaderboard-List/1.1 (Geode mod)");
        m_classicRequest.spawn(classicRequest.get("https://api.demonlist.org/level/classic/list"), [this](web::WebResponse response) {
            onSourceFinished(Source::Classic, std::move(response));
        });
        fetchPointercratePage(0);
    }

    void renderPage() {
        clearRows();
        auto begin = m_page * LEVELS_PER_PAGE;
        auto end = std::min(begin + LEVELS_PER_PAGE, m_visibleEntries.size());
        constexpr float rowTop = 221.f;
        constexpr float rowStep = 22.f;
        for (size_t slot = 0; begin + slot < end; ++slot) {
            auto const& entry = m_entries[m_visibleEntries[begin + slot]];
            auto row = CCLayerColor::create(slot % 2 ? ccc4(23, 34, 52, 240) : ccc4(29, 42, 62, 240));
            row->setContentSize({ 390.f, 21.f });
            row->setPosition({ 25.f, rowTop - static_cast<float>(slot) * rowStep });
            row->setID("leaderboard-level-row"_spr);
            m_rows->addChild(row);

            auto rankNumber = rankFor(entry, m_filter);
            auto rank = CCLabelBMFont::create(rankNumber ? fmt::format("#{}", rankNumber).c_str() : "-", "goldFont.fnt");
            rank->setScale(0.32f);
            rank->setAnchorPoint({ 0.f, 0.5f });
            rank->setPosition({ 6.f, 10.5f });
            row->addChild(rank);

            auto name = CCLabelBMFont::create(entry.name.c_str(), "chatFont.fnt");
            name->setScale(0.45f);
            name->setAnchorPoint({ 0.f, 0.5f });
            name->setPosition({ 43.f, 10.5f });
            name->setWidth(174.f);
            row->addChild(name);

            auto badge = CCLabelBMFont::create(sourceMarks(entry).c_str(), "chatFont.fnt");
            badge->setScale(0.36f);
            badge->setColor({ 128, 193, 213 });
            badge->setAnchorPoint({ 0.f, 0.5f });
            badge->setPosition({ 224.f, 10.5f });
            row->addChild(badge);

            auto menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            row->addChild(menu);
            auto info = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("INFO", 36, true, "goldFont.fnt", "GJ_button_05.png", 21.f, 0.48f),
                this, menu_selector(LeaderboardPopup::onShowDetails)
            );
            info->setTag(static_cast<int>(m_visibleEntries[begin + slot]));
            info->setPosition({ 276.f, 10.5f });
            menu->addChild(info);

            auto copy = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("ID", 34, true, "goldFont.fnt", "GJ_button_05.png", 21.f, 0.50f),
                this, menu_selector(LeaderboardPopup::onCopyID)
            );
            copy->setTag(static_cast<int>(m_visibleEntries[begin + slot]));
            copy->setPosition({ 325.f, 10.5f });
            menu->addChild(copy);

            auto open = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("OPEN", 49, true, "goldFont.fnt", "GJ_button_01.png", 21.f, 0.50f),
                this, menu_selector(LeaderboardPopup::onOpenEntry)
            );
            open->setTag(static_cast<int>(m_visibleEntries[begin + slot]));
            open->setPosition({ 371.f, 10.5f });
            menu->addChild(open);
        }

        auto pages = (m_visibleEntries.size() + LEVELS_PER_PAGE - 1) / LEVELS_PER_PAGE;
        if (m_pageLabel) {
            m_pageLabel->setString(pages ? fmt::format("{} / {}", m_page + 1, pages).c_str() : "0 / 0");
        }
        if (m_pageMenu) m_pageMenu->setVisible(pages > 1);
    }

    void showSelectedLevel(LevelEntry const& entry) {
        auto rankText = [](int rank) { return rank ? fmt::format("#{}", rank) : std::string("—"); };
        auto detail = fmt::format("{}\nID: {}\nAREDL: {}\nClassic: {}\nPointercrate: {}\n{}: {}",
            entry.name, entry.id, rankText(entry.aredlRank), rankText(entry.classicRank),
            rankText(entry.pointercrateRank), tr("Sources", "Sumber"), sourceMarks(entry));
        auto title = tr("LEVEL DETAILS", "DETAIL LEVEL");
        FLAlertLayer::create(title.c_str(), detail.c_str(), "OK")->show();
    }

public:
    static LeaderboardPopup* create() {
        auto ret = new LeaderboardPopup();
        if (ret->init(440.f, 370.f) && ret->setup()) {
            // The popup's natural size is larger than a mobile GD viewport.
            // Fit it to the visible area instead of cropping its title/close button.
            auto visible = CCDirector::sharedDirector()->getWinSize();
            ret->setScale(std::min({ 1.f, (visible.width - 16.f) / 440.f,
                (visible.height - 16.f) / 370.f }));
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void onChooseFilter(CCObject* sender) {
        m_filter = static_cast<Source>(sender->getTag());
        m_page = 0;
        updateFilterStyle();
        if (!m_hasLoaded) return;
        rebuildVisible();
        renderPage();
        auto filterName = m_filter == Source::All ? "ALL"
            : m_filter == Source::Aredl ? "AREDL"
            : m_filter == Source::Classic ? "CLASSIC" : "POINTERCRATE";
        setStatus(fmt::format("{} · {} {}", filterName, m_visibleEntries.size(), tr("levels", "level")));
    }

    void onRefresh(CCObject*) { refreshLists(); }

    void onPreviousPage(CCObject*) {
        if (!m_page) return;
        --m_page;
        renderPage();
    }

    void onNextPage(CCObject*) {
        auto pages = (m_visibleEntries.size() + LEVELS_PER_PAGE - 1) / LEVELS_PER_PAGE;
        if (m_page + 1 >= pages) return;
        ++m_page;
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
        setStatus(result ? fmt::format("{}  {}", tr("Copied ID", "ID disalin"), m_entries[index].id)
                         : tr("Clipboard copy failed.", "Gagal menyalin ke clipboard."));
    }

    void onOpenEntry(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
        if (index >= m_entries.size()) return;
        auto const& entry = m_entries[index];
        int rank = rankFor(entry, m_filter);
        Source pageSource = m_filter;
        if (pageSource == Source::All) {
            pageSource = entry.classicRank ? Source::Classic : entry.aredlRank ? Source::Aredl : Source::Pointercrate;
            rank = rankFor(entry, pageSource);
        }
        if (!rank && entry.classicRank) { pageSource = Source::Classic; rank = entry.classicRank; }
        if (!rank && entry.aredlRank) { pageSource = Source::Aredl; rank = entry.aredlRank; }
        if (!rank && entry.pointercrateRank) { pageSource = Source::Pointercrate; rank = entry.pointercrateRank; }
        openURL(urlFor(pageSource, entry.id, rank));
    }

};

// A menu owns the touch, so taps still use the normal GD menu-item callback.
// Once the finger moves past a short threshold it becomes a drag instead.
class DraggableFabMenu : public CCMenu {
    CCPoint m_touchStart = {};
    CCPoint m_origin = {};
    float m_edge = 45.f;
    bool m_dragged = false;

    CCPoint clamped(CCPoint point) const {
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

class $modify(LeaderboardMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (!Mod::get()->getSettingValue<bool>("show-floating-button")) return true;

        auto icon = CCSprite::create("floating-icon.png"_spr);
        if (!icon) return true;
        auto preference = Mod::get()->getSettingValue<std::string>("floating-size");
        float diameter = preference == "small" ? 46.f : preference == "medium" ? 62.f
            : preference == "extra-large" ? 98.f : 80.f;
        icon->setScale(diameter / icon->getContentSize().width);
        bool showLabel = Mod::get()->getSettingValue<bool>("show-floating-label");
        auto menu = DraggableFabMenu::create(diameter / 2.f + (showLabel ? 20.f : 9.f));
        if (!menu) return true;
        menu->setID("aluxs-leaderboard-fab"_spr);
        auto button = CCMenuItemSpriteExtra::create(icon, this, menu_selector(LeaderboardMenuLayer::onLeaderboard));
        menu->addChild(button);
        if (showLabel) {
            auto label = CCLabelBMFont::create("EXTREME", "goldFont.fnt");
            label->setScale(0.35f);
            label->setPosition({ 0.f, -diameter / 2.f - 9.f });
            menu->addChild(label);
        }
        button->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(1.4f, 1.035f),
            CCScaleTo::create(1.4f, 1.0f),
            nullptr
        )));
        addChild(menu, 100);
        menu->restorePosition();
        return true;
    }

    void onLeaderboard(CCObject*) {
        if (auto popup = LeaderboardPopup::create()) popup->show();
    }
};
