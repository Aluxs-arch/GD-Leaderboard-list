#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <limits>
#include <cmath>
#include <functional>
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
    constexpr size_t LEVELS_PER_PAGE = 6;

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

    bool isGDBlue() {
        return Mod::get()->getSettingValue<std::string>("panel-theme") != "midnight";
    }

    // A restrained Geometry Dash palette: blue tabs, green actions, pale bevels.
    CCNode* actionSurface(char const* label, float width, float height, bool primary = false) {
        auto blue = isGDBlue();
        auto fill = blue ? (primary ? ccc4(46, 143, 89, 255) : ccc4(37, 111, 180, 255))
            : (primary ? ccc4(173, 121, 70, 255) : ccc4(36, 54, 72, 255));
        auto surface = CCLayerColor::create(fill);
        surface->setContentSize({ width, height });
        auto lineColor = blue ? (primary ? ccc4(142, 221, 166, 255) : ccc4(153, 213, 248, 255))
            : (primary ? ccc4(239, 195, 129, 255) : ccc4(92, 127, 142, 255));
        auto line = CCLayerColor::create(lineColor);
        line->setContentSize({ width, 1.f });
        line->setPosition({ 0.f, height - 1.f });
        surface->addChild(line);
        auto text = CCLabelBMFont::create(label, blue ? "bigFont.fnt" : "goldFont.fnt");
        text->setScale(primary ? .40f : .38f);
        text->setPosition({ width / 2.f, height / 2.f });
        surface->addChild(text);
        return surface;
    }

}

class LeaderboardPopup : public geode::Popup, public LevelManagerDelegate {
protected:
    Source m_filter = Source::All;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCNode* m_rows = nullptr;
    CCMenu* m_pageMenu = nullptr;
    CCMenu* m_filterMenu = nullptr;
    std::array<CCMenuItemSpriteExtra*, 4> m_filterButtons{};
    std::array<CCLayerColor*, 4> m_filterAccents{};
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

    bool setup() {
        setTitle(tr("LEADERBOARD LIST", "DAFTAR LEADERBOARD"), "goldFont.fnt", 0.72f);
        auto size = m_mainLayer->getContentSize();
        if (Mod::get()->getSettingValue<bool>("remember-tab")) {
            m_filter = static_cast<Source>(std::clamp(Mod::get()->getSavedValue<int>("last-tab", 0), 0, 3));
        }

        // Popup::init already creates the full-screen black scrim. Hide only
        // its stock brown nine-slice: our resource is the actual foreground.
        m_bgSprite->setVisible(false);
        auto cover = isGDBlue() ? CCSprite::create("leaderboard-bg.png"_spr)
            : CCSprite::create("leaderboard-bg-midnight.png"_spr);
        if (cover) {
            cover->setAnchorPoint({ 0.f, 0.f });
            cover->setPosition({ 0.f, 0.f });
            cover->setScaleX(size.width / cover->getContentSize().width);
            cover->setScaleY(size.height / cover->getContentSize().height);
            cover->setID("leaderboard-cover"_spr);
            m_mainLayer->addChild(cover, -2);
        }

        auto logo = CCSprite::create("extreme-demon.png"_spr);
        if (logo) {
            logo->setScale(52.f / logo->getContentSize().width);
            logo->setPosition({ 48.f, size.height - 50.f });
            logo->setID("leaderboard-brand-mark"_spr);
            m_mainLayer->addChild(logo);
            if (!Mod::get()->getSettingValue<bool>("reduce-motion")) {
                auto aura = CCSprite::create("extreme-demon.png"_spr);
                if (aura) {
                    aura->setScale(65.f / aura->getContentSize().width);
                    aura->setPosition(logo->getPosition());
                    aura->setOpacity(48);
                    m_mainLayer->addChild(aura, -1);
                    aura->runAction(CCRepeatForever::create(CCSequence::create(
                        CCFadeTo::create(1.8f, 95), CCFadeTo::create(1.8f, 32), nullptr
                    )));
                }
            }
        }

        auto brand = CCLabelBMFont::create("EXTREME  /  ALUXS", "goldFont.fnt");
        brand->setScale(0.42f);
        brand->setAnchorPoint({ 0.f, 0.5f });
        brand->setPosition({ 82.f, size.height - 41.f });
        m_mainLayer->addChild(brand);

        auto subtitle = CCLabelBMFont::create(
            tr("Three lists. One direct path to the level.", "Tiga list. Satu jalan langsung ke level.").c_str(),
            "chatFont.fnt"
        );
        subtitle->setScale(0.50f);
        subtitle->setAnchorPoint({ 0.f, 0.5f });
        subtitle->setColor({ 160, 181, 207 });
        subtitle->setPosition({ 82.f, size.height - 61.f });
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
        m_status->setColor(isGDBlue() ? ccColor3B{ 235, 245, 255 } : ccColor3B{ 165, 190, 218 });
        m_status->setAnchorPoint({ 0.f, 0.5f });
        m_status->setPosition({ 25.f, size.height - 118.f });
        m_status->setWidth(310.f);
        m_mainLayer->addChild(m_status);

        auto refreshMenu = CCMenu::create();
        refreshMenu->setPosition({ size.width - 56.f, size.height - 118.f });
        refreshMenu->addChild(CCMenuItemSpriteExtra::create(
            actionSurface(tr("REFRESH", "ULANG").c_str(), 58.f, 22.f, true),
            this, menu_selector(LeaderboardPopup::onRefresh)
        ));
        m_mainLayer->addChild(refreshMenu);

        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows);

        m_pageMenu = CCMenu::create();
        m_pageMenu->setPosition({ size.width / 2.f, 31.f });
        m_pageMenu->setVisible(false);
        m_pageMenu->addChild(CCMenuItemSpriteExtra::create(
            actionSurface("<", 33.f, 21.f),
            this, menu_selector(LeaderboardPopup::onPreviousPage)
        ));
        auto next = CCMenuItemSpriteExtra::create(
            actionSurface(">", 33.f, 21.f),
            this, menu_selector(LeaderboardPopup::onNextPage)
        );
        next->setPosition({ 86.f, 0.f });
        m_pageMenu->addChild(next);
        m_mainLayer->addChild(m_pageMenu);

        m_pageLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_pageLabel->setScale(0.48f);
        m_pageLabel->setColor(isGDBlue() ? ccColor3B{ 235, 245, 255 } : ccColor3B{ 182, 198, 220 });
        m_pageLabel->setPosition({ size.width / 2.f + 43.f, 31.f });
        m_mainLayer->addChild(m_pageLabel);

        auto footer = CCLabelBMFont::create(tr("ALUXS  ·  DRAG THE SHORTCUT TO MOVE", "ALUXS  ·  GESER TOMBOL UNTUK MEMINDAHKAN").c_str(), "chatFont.fnt");
        footer->setScale(0.40f);
        footer->setColor({ 109, 128, 154 });
        footer->setPosition({ size.width / 2.f, 11.f });
        m_mainLayer->addChild(footer);

        refreshLists();
        return true;
    }

    void addFilterButton(char const* label, Source source, float x) {
        auto button = CCMenuItemSpriteExtra::create(
            actionSurface(label, 78.f, 27.f),
            this, menu_selector(LeaderboardPopup::onChooseFilter)
        );
        button->setTag(static_cast<int>(source));
        button->setPosition({ x, 0.f });
        m_filterButtons[static_cast<size_t>(source)] = button;
        auto accent = CCLayerColor::create(isGDBlue() ? ccc4(137, 237, 239, 255) : ccc4(230, 170, 95, 255));
        accent->setContentSize({ 78.f, 2.f });
        accent->setPosition({ 0.f, 0.f });
        button->addChild(accent, 1);
        m_filterAccents[static_cast<size_t>(source)] = accent;
        m_filterMenu->addChild(button);
        updateFilterStyle();
    }

    void updateFilterStyle() {
        for (size_t i = 0; i < m_filterButtons.size(); ++i) {
            if (m_filterAccents[i]) m_filterAccents[i]->setVisible(i == static_cast<size_t>(m_filter));
        }
    }

    void setStatus(std::string const& text) {
        if (m_status) m_status->setString(text.c_str());
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
        m_previewCache.clear();
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

    void attachPreview(CCNode* holder, CCTexture2D* texture) {
        if (!holder || !holder->getParent() || !texture) return;
        holder->removeAllChildrenWithCleanup(true);
        auto sprite = CCSprite::createWithTexture(texture);
        if (!sprite) return;
        // Match the thumbnail window without distorting the source aspect ratio.
        auto imageSize = sprite->getContentSize();
        if (imageSize.width <= 0 || imageSize.height <= 0) return;
        sprite->setScale(std::min(43.f / imageSize.width, 25.f / imageSize.height));
        sprite->setPosition({ 22.f, 13.f });
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
        auto begin = m_page * LEVELS_PER_PAGE;
        auto end = std::min(begin + LEVELS_PER_PAGE, m_visibleEntries.size());
        constexpr float rowTop = 213.f;
        constexpr float rowStep = 31.f;
        for (size_t slot = 0; begin + slot < end; ++slot) {
            auto const& entry = m_entries[m_visibleEntries[begin + slot]];
            auto rowColor = isGDBlue() ? (slot % 2 ? ccc4(27, 83, 143, 250) : ccc4(33, 96, 156, 250))
                : (slot % 2 ? ccc4(22, 37, 55, 250) : ccc4(28, 44, 63, 250));
            auto row = CCLayerColor::create(rowColor);
            row->setContentSize({ 390.f, 29.f });
            row->setPosition({ 25.f, rowTop - static_cast<float>(slot) * rowStep });
            row->setID("leaderboard-level-row"_spr);
            m_rows->addChild(row);
            auto rail = CCLayerColor::create(isGDBlue() ? ccc4(142, 209, 239, 255) : ccc4(181, 127, 78, 255));
            rail->setContentSize({ 2.f, 29.f });
            row->addChild(rail);

            auto rankNumber = rankFor(entry, m_filter);
            auto rank = CCLabelBMFont::create(rankNumber ? fmt::format("#{}", rankNumber).c_str() : "-", "goldFont.fnt");
            rank->setScale(.30f);
            rank->setAnchorPoint({ 0.f, .5f });
            rank->setPosition({ 5.f, 14.5f });
            row->addChild(rank);

            if (Mod::get()->getSettingValue<bool>("show-thumbnails")) {
                auto holder = CCNode::create();
                holder->setContentSize({ 44.f, 26.f });
                holder->setPosition({ 31.f, 1.5f });
                row->addChild(holder);
                // A small editorial cover, keyed to the GD ID. It is deliberately
                // illustrative rather than pretending to be a gameplay screenshot.
                auto palette = std::hash<std::string>{}(entry.id) % 3;
                auto color = palette == 0 ? ccc4(56, 65, 79, 255)
                    : palette == 1 ? ccc4(47, 71, 76, 255) : ccc4(69, 53, 67, 255);
                auto tile = CCLayerColor::create(color);
                tile->setContentSize({ 44.f, 26.f });
                holder->addChild(tile);
                auto mark = CCLayerColor::create(ccc4(221, 170, 102, 255));
                mark->setContentSize({ 2.f, 26.f });
                tile->addChild(mark);
                auto symbol = CCSprite::create("extreme-demon.png"_spr);
                if (symbol) {
                    symbol->setScale(20.f / symbol->getContentSize().width);
                    symbol->setPosition({ 16.f, 13.f });
                    holder->addChild(symbol);
                }
                if (!entry.name.empty() && std::isalnum(static_cast<unsigned char>(entry.name[0])) &&
                    static_cast<unsigned char>(entry.name[0]) < 128) {
                    auto initial = CCLabelBMFont::create(entry.name.substr(0, 1).c_str(), "goldFont.fnt");
                    initial->setScale(.32f);
                    initial->setPosition({ 35.f, 12.f });
                    holder->addChild(initial);
                }
                if (Mod::get()->getSettingValue<bool>("video-previews") && !entry.videoId.empty())
                    loadPreview(holder, entry.videoId);
            }

            auto name = CCLabelBMFont::create(entry.name.empty() ? "Unknown" : entry.name.c_str(), "chatFont.fnt");
            name->setScale(.43f);
            name->setAnchorPoint({ 0.f, .5f });
            name->setPosition({ 80.f, 14.5f });
            name->setWidth(128.f);
            row->addChild(name);

            auto badge = CCLabelBMFont::create(sourceMarks(entry).c_str(), "chatFont.fnt");
            badge->setScale(.34f);
            badge->setColor(isGDBlue() ? ccColor3B{ 185, 232, 250 } : ccColor3B{ 132, 198, 210 });
            badge->setVisible(Mod::get()->getSettingValue<bool>("show-source-marks"));
            badge->setAnchorPoint({ 0.f, .5f });
            badge->setPosition({ 217.f, 14.5f });
            row->addChild(badge);

            auto menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            row->addChild(menu);
            auto info = CCMenuItemSpriteExtra::create(actionSurface("INFO", 43.f, 23.f), this, menu_selector(LeaderboardPopup::onShowDetails));
            info->setTag(static_cast<int>(m_visibleEntries[begin + slot]));
            info->setPosition({ 274.f, 14.5f });
            menu->addChild(info);
            auto copy = CCMenuItemSpriteExtra::create(actionSurface("ID", 34.f, 23.f), this, menu_selector(LeaderboardPopup::onCopyID));
            copy->setTag(static_cast<int>(m_visibleEntries[begin + slot]));
            copy->setPosition({ 318.f, 14.5f });
            menu->addChild(copy);
            auto open = CCMenuItemSpriteExtra::create(actionSurface("OPEN", 49.f, 23.f, true), this, menu_selector(LeaderboardPopup::onOpenEntry));
            open->setTag(static_cast<int>(m_visibleEntries[begin + slot]));
            open->setPosition({ 363.f, 14.5f });
            menu->addChild(open);
        }
        auto pages = (m_visibleEntries.size() + LEVELS_PER_PAGE - 1) / LEVELS_PER_PAGE;
        if (m_pageLabel) m_pageLabel->setString(pages ? fmt::format("{} / {}", m_page + 1, pages).c_str() : "0 / 0");
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
            // Scale the foreground ONLY; scaling Popup itself also shrinks
            // its built-in black scrim, leaving uncovered game at the edges.
            ret->m_mainLayer->setScale(std::min({ 1.f, (visible.width - 16.f) / 440.f,
                (visible.height - 16.f) / 370.f }));
            ret->setOpacity(Mod::get()->getSettingValue<bool>("dim-background") ? 155 : 0);
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void onChooseFilter(CCObject* sender) {
        m_filter = static_cast<Source>(sender->getTag());
        if (Mod::get()->getSettingValue<bool>("remember-tab"))
            Mod::get()->setSavedValue("last-tab", static_cast<int>(m_filter));
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

    void onOpenEntry(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
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

class $modify(LeaderboardMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (!Mod::get()->getSettingValue<bool>("show-floating-button")) return true;

        auto icon = CCSprite::create("floating-icon.png"_spr);
        if (!icon) return true;
        auto preference = Mod::get()->getSettingValue<std::string>("floating-size");
        float diameter = preference == "very-small" ? 24.f : preference == "medium" ? 44.f
            : preference == "large" ? 58.f : 32.f;
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
        if (!Mod::get()->getSettingValue<bool>("reduce-motion")) {
            button->runAction(CCRepeatForever::create(CCSequence::create(
                CCScaleTo::create(1.6f, 1.035f),
                CCScaleTo::create(1.6f, 1.0f), nullptr
            )));
        }
        addChild(menu, 100);
        menu->restorePosition();
        return true;
    }

    void onLeaderboard(CCObject*) {
        if (auto popup = LeaderboardPopup::create()) popup->show();
    }
};
