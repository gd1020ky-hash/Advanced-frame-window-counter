// AFWC mod menu: the actions (use attempt / scan / run details / help) and the most used options in one popup.
// Every switch and stepper here edits the same settings as Geode's own settings page ("All settings").
#include "../app/App.hpp"
#include "../hooks/ScanDriver.hpp"
#include "Ui.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace afwc::ui {

namespace {

class MenuPopup;
MenuPopup* g_menu = nullptr;

class MenuPopup : public Popup {
    CCMenu* m_top = nullptr;
    CCNode* m_pages[2] = {nullptr, nullptr};
    CCMenu* m_menus[2] = {nullptr, nullptr};
    ButtonSprite* m_tabs[2] = {nullptr, nullptr};

    bool setup() {
        if (!Popup::init(400.f, 270.f)) return false;
        setTitle("AFWC", "goldFont.fnt", 0.8f, 18.f);
        auto size = m_mainLayer->getContentSize();

        m_top = CCMenu::create();
        m_top->setPosition({0.f, 0.f});
        m_mainLayer->addChild(m_top);

        // actions, always visible
        float ay = size.height - 48.f;
        float xs[4] = {70.f, 160.f, 250.f, 340.f};
        textButton(m_top, "Use attempt", {xs[0], ay}, 0.5f, [] { useAttempt(); });
        textButton(m_top, "Scan", {xs[1], ay}, 0.5f, [this] {
            closeMenu();
            startScanFlow();
        });
        textButton(m_top, "Run details", {xs[2], ay}, 0.5f, [this] {
            closeMenu();
            openPanel();
        });
        textButton(m_top, "Help", {xs[3], ay}, 0.5f, [] { showHelp(); });
        textButton(m_top, "All settings", {size.width / 2, 24.f}, 0.45f, [] { openSettingsPopup(Mod::get(), false); });

        // tabs
        char const* names[2] = {"General", "Scanner"};
        for (int i = 0; i < 2; i++) {
            m_tabs[i] = ButtonSprite::create(names[i]);
            m_tabs[i]->setScale(0.45f);
            auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(m_tabs[i], [this, i](CCMenuItemSpriteExtra*) { showTab(i); });
            item->setPosition({size.width / 2 + (i == 0 ? -55.f : 55.f), size.height - 76.f});
            m_top->addChild(item);

            m_pages[i] = CCNode::create();
            m_mainLayer->addChild(m_pages[i]);
            m_menus[i] = CCMenu::create();
            m_menus[i]->setPosition({0.f, 0.f});
            m_pages[i]->addChild(m_menus[i]);
        }

        float hy = size.height - 100.f;     // section headers
        float r0 = size.height - 122.f;     // first row
        float sp = 24.f;                    // row spacing

        // ---- page 0: what is shown, what is recorded ----
        header(0, "Display", {30.f, hy});
        header(0, "Recording", {215.f, hy});
        toggle(0, "Counter HUD", "show-hud", {36.f, r0});
        toggle(0, "Click markers", "show-markers", {36.f, r0 - sp});
        toggle(0, "Early/late detail", "marker-detail", {36.f, r0 - 2 * sp});
        toggle(0, "Info line", "show-info-line", {36.f, r0 - 3 * sp});
        toggle(0, "Enabled", "enabled", {216.f, r0});
        toggle(0, "Record every attempt", "auto-record", {216.f, r0 - sp});
        toggle(0, "Save completed runs", "auto-save-complete", {216.f, r0 - 2 * sp});
        note(0, "HUD / marker size and the TPS are in All settings.", {216.f, r0 - 3 * sp});

        // ---- page 1: how the frame analysis measures ----
        header(1, "Frame analysis", {30.f, hy});
        stepper(1, "Search radius (ticks)", "scan-max-shift", 4, 240, 5, r0);
        stepper(1, "Survive after next click", "scan-horizon", -1, 480, 2, r0 - sp);
        shiftToggle(1, {36.f, r0 - 2 * sp});
        toggle(1, "Verify determinism", "scan-verify", {216.f, r0 - 2 * sp});
        toggle(1, "Mute while scanning", "scan-mute", {36.f, r0 - 3 * sp});
        note(1, "Radius: how far each click is moved, per side.", {30.f, r0 - 4 * sp + 6.f});
        note(1, "Survive: ticks the player must live after the next click (end = to the end of the run).", {30.f, r0 - 4 * sp - 6.f});

        showTab(0);
        return true;
    }

    void showTab(int i) {
        for (int k = 0; k < 2; k++) {
            m_pages[k]->setVisible(k == i);
            m_tabs[k]->setColor(k == i ? ccColor3B{255, 255, 255} : ccColor3B{120, 120, 120});
        }
    }

    void header(int page, char const* text, CCPoint pos) {
        auto* l = CCLabelBMFont::create(text, "goldFont.fnt");
        l->setScale(0.45f);
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition(pos);
        m_pages[page]->addChild(l);
    }

    void note(int page, char const* text, CCPoint pos) {
        auto* l = CCLabelBMFont::create(text, "chatFont.fnt");
        l->setScale(0.4f);
        l->setColor({190, 190, 190});
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition(pos);
        m_pages[page]->addChild(l);
    }

    CCLabelBMFont* rowLabel(int page, char const* text, CCPoint pos) {
        auto* l = CCLabelBMFont::create(text, "bigFont.fnt");
        l->setScale(0.32f);
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition(pos);
        m_pages[page]->addChild(l);
        return l;
    }

    // on/off switch bound to a bool setting
    void toggle(int page, char const* label, char const* key, CCPoint pos) {
        auto* tg = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(0.5f, [key](CCMenuItemToggler* t) {
            Mod::get()->setSettingValue<bool>(key, !t->isToggled());
            app().revision++;
        });
        tg->toggle(opt<bool>(key));
        tg->setPosition(pos);
        m_menus[page]->addChild(tg);
        rowLabel(page, label, {pos.x + 16.f, pos.y});
    }

    // "Shift mode" is a string setting: on = press-only, off = whole-tap
    void shiftToggle(int page, CCPoint pos) {
        auto* tg = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(0.5f, [](CCMenuItemToggler* t) {
            Mod::get()->setSettingValue<std::string>("scan-shift-mode", !t->isToggled() ? std::string("press-only") : std::string("whole-tap"));
        });
        tg->toggle(opt<std::string>("scan-shift-mode") == "press-only");
        tg->setPosition(pos);
        m_menus[page]->addChild(tg);
        rowLabel(page, "Move only the press", {pos.x + 16.f, pos.y});
    }

    // - / + buttons bound to an int setting; a negative value reads "end"
    void stepper(int page, char const* label, char const* key, int64_t lo, int64_t hi, int64_t step, float y) {
        rowLabel(page, label, {30.f, y});
        auto* val = CCLabelBMFont::create("", "bigFont.fnt");
        val->setScale(0.38f);
        val->setPosition({275.f, y});
        m_pages[page]->addChild(val);
        auto refresh = [val, key]() {
            int64_t v = opt<int64_t>(key);
            val->setString(v < 0 ? "end" : fmt::format("{}", v).c_str());
        };
        refresh();
        auto change = [key, lo, hi, refresh](int64_t d) {
            int64_t v = std::clamp<int64_t>(opt<int64_t>(key) + d, lo, hi);
            Mod::get()->setSettingValue<int64_t>(key, v);
            refresh();
        };
        auto* minus = geode::cocos::CCMenuItemExt::createSpriteExtraWithFrameName("edit_leftBtn_001.png", 0.5f, [change, step](auto*) { change(-step); });
        minus->setPosition({235.f, y});
        m_menus[page]->addChild(minus);
        auto* plus = geode::cocos::CCMenuItemExt::createSpriteExtraWithFrameName("edit_rightBtn_001.png", 0.5f, [change, step](auto*) { change(step); });
        plus->setPosition({315.f, y});
        m_menus[page]->addChild(plus);
    }

    void onClose(CCObject* s) override {
        g_menu = nullptr;
        Popup::onClose(s);
    }

public:
    void closeMenu() { onClose(nullptr); }
    static MenuPopup* create() {
        auto* r = new (std::nothrow) MenuPopup();
        if (r && r->setup()) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }
    ~MenuPopup() override { if (g_menu == this) g_menu = nullptr; }
};

}  // namespace

void openMenu() {
    if (g_menu) { g_menu->closeMenu(); return; }
    if (scan::active()) return;
    g_menu = MenuPopup::create();
    if (g_menu) g_menu->show();
}

}  // namespace afwc::ui

===== src/ui/Panel.cpp =====
// AFWC main panel: what the active run measured, the click list, and the actions (use attempt / scan / ...).
#include "../app/App.hpp"
#include "../hooks/ScanDriver.hpp"
#include "Ui.hpp"
#include <algorithm>

using namespace geode::prelude;

namespace afwc::ui {

namespace {

std::string fmtTime(Run const& r, int32_t tick) {
    return fmt::format("{:.2f}s", double(tick) / (r.tps > 0 ? r.tps : 240.0));
}

class Panel;
Panel* g_panel = nullptr;

class Panel : public Popup {
    static constexpr int ROWS = 6;
    int m_page = 0;
    uint32_t m_rev = 0;
    CCLabelBMFont *m_head = nullptr, *m_sub = nullptr, *m_check = nullptr, *m_pageLbl = nullptr;
    CCNode* m_rows = nullptr;
    CCMenu* m_menu = nullptr;
    float m_timer = 0;

    bool setup() {
        if (!Popup::init(450.f, 300.f)) return false;
        setTitle("AFWC", "goldFont.fnt", 0.8f, 18.f);
        auto size = m_mainLayer->getContentSize();
        m_menu = CCMenu::create();
        m_menu->setPosition({0, 0});
        m_mainLayer->addChild(m_menu);

        // master switch
        auto* tg = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(0.5f, [](CCMenuItemToggler* t) {
            Mod::get()->setSettingValue<bool>("enabled", !t->isToggled());
            app().revision++;
        });
        tg->toggle(opt<bool>("enabled"));
        tg->setPosition({26.f, size.height - 24.f});
        m_menu->addChild(tg);
        auto* en = CCLabelBMFont::create("On", "bigFont.fnt");
        en->setScale(0.35f);
        en->setAnchorPoint({0.f, 0.5f});
        en->setPosition({38.f, size.height - 24.f});
        m_mainLayer->addChild(en);

        m_head = CCLabelBMFont::create("", "chatFont.fnt");
        m_head->setScale(0.5f);
        m_head->setPosition({size.width / 2, size.height - 42.f});
        m_mainLayer->addChild(m_head);
        m_sub = CCLabelBMFont::create("", "chatFont.fnt");
        m_sub->setScale(0.45f);
        m_sub->setPosition({size.width / 2, size.height - 56.f});
        m_mainLayer->addChild(m_sub);
        m_check = CCLabelBMFont::create("", "chatFont.fnt");
        m_check->setScale(0.45f);
        m_check->setPosition({size.width / 2, size.height - 69.f});
        m_mainLayer->addChild(m_check);

        float y1 = size.height - 94.f, y2 = size.height - 120.f;
        float xs[4] = {75.f, 165.f, 255.f, 345.f};
        textButton(m_menu, "Use attempt", {xs[0], y1}, 0.5f, [] { useAttempt(); });
        textButton(m_menu, "Scan", {xs[1], y1}, 0.5f, [this] {
            this->onClose(nullptr);
            startScanFlow();
        });
        textButton(m_menu, "Settings", {xs[2], y1}, 0.5f, [] { openSettingsPopup(Mod::get(), false); });
        textButton(m_menu, "Help", {xs[3], y1}, 0.5f, [] { showHelp(); });
        textButton(m_menu, "All on", {xs[0], y2}, 0.4f, [this] { setAll(true); });
        textButton(m_menu, "All off", {xs[1], y2}, 0.4f, [this] { setAll(false); });
        textButton(m_menu, "Clear run", {xs[2], y2}, 0.4f, [this] { clearAll(); });

        m_rows = CCNode::create();
        m_mainLayer->addChild(m_rows);

        // paging
        auto* prev = geode::cocos::CCMenuItemExt::createSpriteExtraWithFrameName("edit_leftBtn_001.png", 0.5f, [this](auto*) { page(-1); });
        prev->setPosition({size.width / 2 - 60.f, 20.f});
        m_menu->addChild(prev);
        auto* next = geode::cocos::CCMenuItemExt::createSpriteExtraWithFrameName("edit_rightBtn_001.png", 0.5f, [this](auto*) { page(1); });
        next->setPosition({size.width / 2 + 60.f, 20.f});
        m_menu->addChild(next);
        m_pageLbl = CCLabelBMFont::create("", "bigFont.fnt");
        m_pageLbl->setScale(0.4f);
        m_pageLbl->setPosition({size.width / 2, 20.f});
        m_mainLayer->addChild(m_pageLbl);

        m_rev = app().revision;
        rebuild();
        this->scheduleUpdate();
        return true;
    }

    void setAll(bool on) {
        for (auto& c : app().run.clicks) c.shouldDraw = on;
        touchRun(true);
        m_rev = app().revision;
        rebuild();
    }

    void clearAll() {
        createQuickPopup("Clear run", "Remove <cr>all</c> clicks of the active run?", "Cancel", "Clear", [this](FLAlertLayer*, bool yes) {
            if (!yes) return;
            clearActiveRun();
            m_rev = app().revision;
            m_page = 0;
            rebuild();
        });
    }

    int pages() const { return std::max(1, int((app().run.clicks.size() + ROWS - 1) / ROWS)); }
    void page(int d) {
        m_page = std::clamp(m_page + d, 0, pages() - 1);
        rebuildRows();
    }

    void refreshHeader() {
        auto& a = app();
        if (!a.hasRun) {
            m_head->setString("No run yet - play a level, die or finish, then press Use attempt");
            m_sub->setString("");
            m_check->setString("");
            return;
        }
        std::string lv = a.run.levelName.empty() ? (a.run.levelID ? fmt::format("level {}", a.run.levelID) : std::string("no level")) : a.run.levelName;
        m_head->setString(fmt::format("{} | {}", lv, describeRun(a.run)).c_str());
        auto const& s = a.summary;
        if (s.withWindow > 0) {
            // counters per label, exactly what the HUD shows at the end of the run
            std::string counts;
            for (auto const& lp : a.labels) {
                int n = 0;
                for (auto const& c : a.run.clicks)
                    if (c.shouldDraw && c.hasWindow() && lp.matches(c.window(), c.ifCount)) n++;
                counts += fmt::format("{} {}   ", lp.text, n);
            }
            m_sub->setString(fmt::format("tightest {} | median {} | {}", formatWindow(s.minWindow), formatWindow(s.medianWindow), counts).c_str());
        } else {
            m_sub->setString(a.run.scannable ? "No windows yet - open this panel in the level and press Scan" : "This run cannot be scanned");
        }
        if (a.run.replayCheck == RC_Match) {
            m_check->setColor({120, 255, 150});
            m_check->setString(a.run.alignShift == 0 ? "Check: the replay reproduced your attempt exactly"
                                                     : fmt::format("Check: reproduced after aligning inputs by {:+} tick", a.run.alignShift).c_str());
        } else if (a.run.replayCheck == RC_Mismatch) {
            m_check->setColor({255, 120, 100});
            m_check->setString("Check: the replay did NOT reproduce your attempt - windows are not trustworthy");
        } else {
            m_check->setColor({200, 200, 200});
            m_check->setString("");
        }
    }

    void update(float dt) override {
        m_timer += dt;
        if (app().revision != m_rev) { m_rev = app().revision; rebuild(); return; }
        if (m_timer > 0.5f) { m_timer = 0; refreshHeader(); }
    }

    void rebuild() {
        m_page = std::clamp(m_page, 0, pages() - 1);
        refreshHeader();
        rebuildRows();
    }

    static std::string statusOf(Click const& c) {
        if (c.measured()) return (c.flags & (CF_EarlyCapped | CF_LateCapped)) ? fmt::format(">= -{}/+{}", c.early, c.late) : fmt::format("-{}/+{} ok", c.early, c.late);
        if (c.flags & CF_Unreachable) return "end";
        if (c.scanned()) return "?";
        return "-";
    }

    void rebuildRows() {
        m_rows->removeAllChildren();
        auto& a = app();
        auto size = m_mainLayer->getContentSize();
        m_pageLbl->setString(fmt::format("{}/{}", m_page + 1, pages()).c_str());
        auto* menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_rows->addChild(menu);
        int start = m_page * ROWS;
        for (int r = 0; r < ROWS; r++) {
            int idx = start + r;
            if (idx >= int(a.run.clicks.size())) break;
            float y = size.height - 152.f - float(r) * 20.f;
            auto* bg = CCScale9Sprite::create("square02_small.png");
            bg->setContentSize({420.f, 18.f});
            bg->setOpacity(90);
            bg->setPosition({size.width / 2, y});
            m_rows->addChild(bg);
            auto const& c = a.run.clicks[idx];

            auto* l = CCLabelBMFont::create(fmt::format("#{}  t{}  {}", idx + 1, c.tick, fmtTime(a.run, c.tick)).c_str(), "chatFont.fnt");
            l->setScale(0.42f);
            l->setAnchorPoint({0.f, 0.5f});
            l->setPosition({24.f, y});
            m_rows->addChild(l);

            auto* pl = CCLabelBMFont::create(c.p2 ? "2P" : "1P", "chatFont.fnt");
            pl->setScale(0.42f);
            pl->setPosition({150.f, y});
            m_rows->addChild(pl);

            auto* tg = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(0.35f, [idx](CCMenuItemToggler* t) {
                if (idx < int(app().run.clicks.size())) app().run.clicks[idx].shouldDraw = !t->isToggled();
                touchRun(false);
            });
            tg->toggle(c.shouldDraw);
            tg->setPosition({178.f, y});
            menu->addChild(tg);

            auto* win = CCLabelBMFont::create(c.hasWindow() ? formatWindow(c.window()).c_str() : "-", "bigFont.fnt");
            win->setScale(0.38f);
            win->setPosition({230.f, y});
            if (c.hasWindow()) {
                Color4 col; std::string txt;
                markerLook(c, col, txt);
                win->setColor({GLubyte(col.r * 255.f), GLubyte(col.g * 255.f), GLubyte(col.b * 255.f)});
            }
            m_rows->addChild(win);

            auto* st = CCLabelBMFont::create(statusOf(c).c_str(), "chatFont.fnt");
            st->setScale(0.42f);
            st->setPosition({320.f, y});
            if (c.measured()) st->setColor({120, 255, 150});
            else if (c.scanned() || (c.flags & CF_Unreachable)) st->setColor({255, 190, 90});
            m_rows->addChild(st);
        }
    }

    void onClose(CCObject* s) override {
        if (app().hasRun) touchRun(true);
        g_panel = nullptr;
        Popup::onClose(s);
    }

public:
    void closePanel() { onClose(nullptr); }
    static Panel* create() {
        auto* r = new (std::nothrow) Panel();
        if (r && r->setup()) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }
    ~Panel() override { if (g_panel == this) g_panel = nullptr; }
};

}  // namespace

void showHelp() {
    MDPopup::create("AFWC help",
        "**1. Record.** Just play. AFWC remembers the clicks of every attempt. When you beat a level the run is saved "
        "automatically; after a death press **Use attempt** to keep that attempt.\n\n"
        "**2. Frame analysis.** Open the AFWC menu from the pause menu of the level and press **Scan**. AFWC replays your run "
        "inside the level and moves every click earlier and later, one tick at a time, until the run stops working. The number "
        "of ticks that still work is the **frame window** of that click (early + late + 1).\n\n"
        "**3. Frame window counter.** When you play the level again, every click shows its window on the player, and the "
        "counters in the top-left corner count how many 1F / 2F / 3F / 4-6F / 7F+ clicks you have passed.\n\n"
        "**Run details** lists every click with its window. Marks: `ok` measured and verified, `>=` lower bound (search radius "
        "reached or a neighbouring click is in the way), `?` not verified, `end` after the replay ended.",
        "OK")->show();
}

void useAttempt() {
    auto& a = app();
    Run r;
    if (a.hasLastAttempt) r = a.lastAttempt;
    else if (!a.live.empty()) r = runFromLive(PlayLayer::get() ? PlayLayer::get()->m_level : nullptr, OUT_Unknown, a.liveTick);
    if (r.clicks.empty()) { notify("No attempt recorded yet. Play, die or finish, then try again.", true); return; }
    setActiveRun(std::move(r), true);
    notify("Attempt loaded");
}

void openPanel() {
    if (g_panel) { g_panel->closePanel(); return; }
    if (scan::active()) return;
    g_panel = Panel::create();
    if (g_panel)
