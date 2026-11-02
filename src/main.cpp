#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/general.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
    enum class Source : int { Aredl = 0, Classic = 1, Pointercrate = 2 };

    struct LevelEntry {
        std::string name;
        std::string id;
        std::string rank;
        std::string sourceURL;
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

    std::string levelURL(Source source, std::string const& id) {
        switch (source) {
            case Source::Aredl: return "https://aredl.net/list/" + id;
            case Source::Classic: return "https://demonlist.org/classic";
            case Source::Pointercrate: return "https://pointercrate.com/demonlist/";
        }
        return "https://aredl.net/list/" + id;
    }

    void openURL(std::string const& url) {
        utils::web::openLinkInBrowser(url);
    }
}

class LeaderboardPopup : public geode::Popup<> {
protected:
    Source m_source = Source::Aredl;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCNode* m_rows = nullptr;
    CCMenu* m_pageMenu = nullptr;
    size_t m_page = 0;
    CCMenu* m_sourceMenu = nullptr;
    TaskHolder<web::WebResponse> m_request;
    std::vector<LevelEntry> m_entries;

    bool init() {
        if (!Popup::init(380.f, 286.f)) return false;
        this->setTitle(tr("LEADERBOARD LIST", "DAFTAR LEADERBOARD"), "goldFont.fnt", 0.72f);

        auto size = m_mainLayer->getContentSize();
        auto panel = CCLayerColor::create(ccc4(14, 21, 37, 220));
        panel->setContentSize({ 350.f, 224.f });
        panel->setPosition({ 15.f, 12.f });
        m_mainLayer->addChild(panel, -1);

        auto brand = CCLabelBMFont::create("ALUXS  /  EXTREME DEMON HUB", "goldFont.fnt");
        brand->setScale(0.38f);
        brand->setAnchorPoint({ 0.f, 0.5f });
        brand->setPosition({ 20.f, size.height - 42.f });
        m_mainLayer->addChild(brand);

        auto subtitle = CCLabelBMFont::create(
            tr("Live list data · copy IDs · open levels", "Data live · salin ID · buka level").c_str(),
            "chatFont.fnt"
        );
        subtitle->setScale(0.52f);
        subtitle->setAnchorPoint({ 0.f, 0.5f });
        subtitle->setPosition({ 20.f, size.height - 61.f });
        subtitle->setColor({ 168, 190, 215 });
        m_mainLayer->addChild(subtitle);

        m_sourceMenu = CCMenu::create();
        m_sourceMenu->setPosition({ size.width / 2.f, size.height - 91.f });
        m_mainLayer->addChild(m_sourceMenu);
        addSourceButton("AREDL", Source::Aredl, -102.f);
        addSourceButton("CLASSIC", Source::Classic, 0.f);
        addSourceButton("POINTERCRATE", Source::Pointercrate, 102.f);

        m_status = CCLabelBMFont::create("Choose a list to load", "chatFont.fnt");
        m_status->setScale(0.58f);
        m_status->setColor({ 164, 189, 218 });
        m_status->setPosition({ 145.f, size.height - 119.f });
        m_status->setWidth(220.f);
        m_mainLayer->addChild(m_status);

        m_rows = CCNode::create();
        m_rows->setPosition({ 0.f, 0.f });
        m_mainLayer->addChild(m_rows);

        auto pageMenu = CCMenu::create();
        pageMenu->setPosition({ size.width / 2.f, 29.f });
        pageMenu->addChild(CCMenuItemSpriteExtra::create(
            ButtonSprite::create("<", 34, true, "goldFont.fnt", "GJ_button_05.png", 24.f, 0.6f),
            this, menu_selector(LeaderboardPopup::onPreviousPage)
        ));
        auto next = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(">", 34, true, "goldFont.fnt", "GJ_button_05.png", 24.f, 0.6f),
            this, menu_selector(LeaderboardPopup::onNextPage)
        );
        next->setPosition({ 78.f, 0.f });
        pageMenu->addChild(next);
        m_pageMenu = pageMenu;
        m_mainLayer->addChild(pageMenu);
        m_pageLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_pageLabel->setScale(0.48f);
        m_pageLabel->setColor({ 176, 194, 216 });
        m_pageLabel->setPosition({ size.width / 2.f + 35.f, 29.f });
        m_mainLayer->addChild(m_pageLabel);

        auto refreshMenu = CCMenu::create();
        refreshMenu->setPosition({ size.width - 55.f, size.height - 118.f });
        auto refresh = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(tr("REFRESH", "MUAT ULANG").c_str(), 82, true, "goldFont.fnt", "GJ_button_01.png", 28.f, 0.65f),
            this,
            menu_selector(LeaderboardPopup::onRefresh)
        );
        refreshMenu->addChild(refresh);
        m_mainLayer->addChild(refreshMenu);

        auto footer = CCLabelBMFont::create("Aluxs-arch  ·  GitHub  ·  Aluxs", "chatFont.fnt");
        footer->setScale(0.42f);
        footer->setColor({ 116, 134, 157 });
        footer->setPosition({ size.width / 2.f, 11.f });
        m_mainLayer->addChild(footer);

        loadSource(Source::Aredl);
        return true;
    }

    void addSourceButton(char const* label, Source source, float x) {
        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(label, 84, true, "goldFont.fnt", "GJ_button_05.png", 30.f, 0.62f),
            this,
            menu_selector(LeaderboardPopup::onChooseSource)
        );
        button->setTag(static_cast<int>(source));
        button->setPosition({ x, 0.f });
        m_sourceMenu->addChild(button);
    }

    void setStatus(std::string const& text) {
        if (!m_status) return;
        m_status->setString(text.c_str());
    }

    void clearRows() {
        if (m_rows) m_rows->removeAllChildrenWithCleanup(true);
    }

    void loadSource(Source source) {
        m_source = source;
        m_page = 0;
        if (m_pageMenu) m_pageMenu->setVisible(source != Source::Classic);
        if (m_pageLabel) {
            m_pageLabel->setString("");
            m_pageLabel->setVisible(source != Source::Classic);
        }
        clearRows();
        m_entries.clear();
        if (source == Source::Classic) {
            setStatus(tr("Classic data is not wired into this build yet.", "Data Classic belum terintegrasi di versi ini."));
            auto info = CCLabelBMFont::create(
                tr("Use OPEN CLASSIC to visit the official list. AREDL and Pointercrate load inside the mod.",
                   "Pilih OPEN CLASSIC untuk membuka list resmi. AREDL dan Pointercrate dimuat di dalam mod.").c_str(),
                "chatFont.fnt"
            );
            info->setScale(0.58f);
            info->setColor({ 200, 211, 226 });
            info->setWidth(305.f);
            info->setAlignment(kCCTextAlignmentCenter);
            info->setPosition({ 190.f, 112.f });
            m_rows->addChild(info);
            auto menu = CCMenu::create();
            menu->setPosition({ 190.f, 76.f });
            menu->addChild(CCMenuItemSpriteExtra::create(
                ButtonSprite::create("OPEN CLASSIC", 110, true, "goldFont.fnt", "GJ_button_01.png", 32.f, 0.62f),
                this, menu_selector(LeaderboardPopup::onOpenClassic)
            ));
            m_rows->addChild(menu);
            return;
        }

        setStatus(tr("Connecting to list…", "Menghubungkan ke leaderboard…"));
        std::string endpoint = source == Source::Aredl
            ? "https://api.aredl.net/api/aredl/levels"
            : "https://pointercrate.com/api/v2/demons/listed/";
        auto req = web::WebRequest();
        req.timeout(std::chrono::seconds(20));
        req.userAgent("GD-Leaderboard-list/1.0 (Geode mod)");
        m_request.spawn(req.get(endpoint), [this, source](web::WebResponse response) {
            if (!response.ok()) {
                setStatus(tr("Could not load the list. Check your connection and retry.", "Gagal memuat list. Periksa koneksi lalu coba lagi."));
                return;
            }
            auto parsed = response.json();
            if (!parsed) {
                setStatus(tr("The list returned invalid data.", "Data dari list tidak valid."));
                return;
            }
            auto data = parsed.unwrap();
            if (!data.isArray()) {
                setStatus(tr("Unexpected list response.", "Format respons list tidak dikenali."));
                return;
            }

            for (size_t i = 0; i < data.size(); ++i) {
                auto item = data[i];
                LevelEntry entry;
                entry.name = jsonText(item, "name");
                // AREDL and Pointercrate distinguish their own record ID from
                // the Geometry Dash level ID. Prefer explicit GD ID fields.
                entry.id = jsonText(item, "level_id");
                if (entry.id.empty()) entry.id = jsonText(item, "gd_id");
                auto rank = jsonText(item, "position");
                if (rank.empty()) rank = jsonText(item, "rank");
                entry.rank = rank.empty() ? std::to_string(i + 1) : rank;
                entry.sourceURL = source == Source::Pointercrate
                    ? "https://pointercrate.com/demonlist/" + (rank.empty() ? std::to_string(i + 1) : rank)
                    : levelURL(source, entry.id);
                if (entry.name.empty()) entry.name = tr("Unknown level", "Level tidak diketahui");
                m_entries.push_back(entry);
            }
            if (m_entries.empty()) {
                setStatus(tr("No levels were returned.", "Tidak ada level yang diterima."));
                return;
            }
            setStatus(fmt::format("{}  ·  {}", source == Source::Aredl ? "AREDL" : "POINTERCRATE", tr("live levels", "level live")));
            renderRows();
        });
    }

    void renderRows() {
        clearRows();
        constexpr size_t pageSize = 5;
        auto start = m_page * pageSize;
        auto end = std::min(start + pageSize, m_entries.size());
        float y = 145.f;
        for (size_t i = start; i < end; ++i) {
            auto const& entry = m_entries[i];
            auto row = CCLayerColor::create(ccc4(31, 43, 63, 230));
            row->setContentSize({ 330.f, 25.f });
            row->setPosition({ 25.f, y - static_cast<float>(i - start) * 22.f });
            m_rows->addChild(row);

            auto rank = CCLabelBMFont::create(fmt::format("#{}", entry.rank).c_str(), "goldFont.fnt");
            rank->setScale(0.38f);
            rank->setAnchorPoint({ 0.f, 0.5f });
            rank->setPosition({ 8.f, 15.f });
            row->addChild(rank);

            auto name = CCLabelBMFont::create(entry.name.c_str(), "chatFont.fnt");
            name->setScale(0.52f);
            name->setAnchorPoint({ 0.f, 0.5f });
            name->setPosition({ 46.f, 15.f });
            name->setWidth(145.f);
            row->addChild(name);

            auto menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            row->addChild(menu);
            auto copy = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("ID", 36, true, "goldFont.fnt", "GJ_button_05.png", 22.f, 0.58f),
                this, menu_selector(LeaderboardPopup::onCopyID)
            );
            copy->setTag(static_cast<int>(i));
            copy->setPosition({ 236.f, 15.f });
            menu->addChild(copy);
            auto open = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("OPEN", 52, true, "goldFont.fnt", "GJ_button_01.png", 25.f, 0.58f),
                this, menu_selector(LeaderboardPopup::onOpenEntry)
            );
            open->setTag(static_cast<int>(i));
            open->setPosition({ 290.f, 15.f });
            menu->addChild(open);
        }
        if (m_pageLabel) {
            auto pages = (m_entries.size() + pageSize - 1) / pageSize;
            m_pageLabel->setString(fmt::format("{} / {}", m_page + 1, pages).c_str());
        }
    }

public:
    static LeaderboardPopup* create() {
        auto ret = new LeaderboardPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void onChooseSource(CCObject* sender) {
        loadSource(static_cast<Source>(sender->getTag()));
    }

    void onRefresh(CCObject*) { loadSource(m_source); }

    void onPreviousPage(CCObject*) {
        if (m_page == 0) return;
        --m_page;
        renderRows();
    }

    void onNextPage(CCObject*) {
        auto pages = (m_entries.size() + 4) / 5;
        if (m_page + 1 >= pages) return;
        ++m_page;
        renderRows();
    }

    void onCopyID(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
        if (index >= m_entries.size() || m_entries[index].id.empty()) {
            setStatus(tr("This list does not provide a level ID.", "List ini tidak menyediakan ID level."));
            return;
        }
        auto result = utils::clipboard::write(m_entries[index].id);
        setStatus(result ? tr("Level ID copied to clipboard ✓", "ID level disalin ke clipboard ✓")
                         : tr("Clipboard copy failed.", "Gagal menyalin ke clipboard."));
    }

    void onOpenEntry(CCObject* sender) {
        auto index = static_cast<size_t>(sender->getTag());
        if (index >= m_entries.size()) return;
        openURL(m_entries[index].sourceURL);
    }

    void onOpenClassic(CCObject*) { openURL("https://demonlist.org/classic"); }
};

class $modify(LeaderboardMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (!Mod::get()->getSettingValue<bool>("show-floating-button")) return true;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({ winSize.width - 52.f, 94.f });
        menu->setID("aluxs-leaderboard-fab");
        auto button = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("EXTREME", 90, true, "goldFont.fnt", "GJ_button_06.png", 42.f, 0.72f),
            this,
            menu_selector(LeaderboardMenuLayer::onLeaderboard)
        );
        menu->addChild(button);
        button->runAction(CCRepeatForever::create(CCSequence::create(
            CCScaleTo::create(1.15f, 1.045f),
            CCScaleTo::create(1.15f, 1.0f),
            nullptr
        )));
        this->addChild(menu, 100);
        return true;
    }

    void onLeaderboard(CCObject*) {
        LeaderboardPopup::create()->show();
    }
};
