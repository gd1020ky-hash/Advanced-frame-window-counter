// The popup that runs a scan: it owns the schedule that drives afwc::scan::drive() once per rendered frame.
#include "../app/App.hpp"
#include "../hooks/ScanDriver.hpp"
#include "Ui.hpp"

using namespace geode::prelude;

namespace afwc::ui {

namespace {

class ScanPopup : public Popup {
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_detail = nullptr;
    CCLayerColor* m_fill = nullptr;
    float m_barW = 260.f;
    bool m_cancelling = false;

    bool setup() {
        if (!Popup::init(320.f, 170.f)) return false;
        setTitle("Scanning frame windows");
        auto size = m_mainLayer->getContentSize();
        m_status = CCLabelBMFont::create("Starting...", "bigFont.fnt");
        m_status->setScale(0.45f);
        m_status->setPosition({size.width / 2, size.height - 52.f});
        m_mainLayer->addChild(m_status);

        auto* bg = CCLayerColor::create({20, 20, 20, 200}, m_barW + 4.f, 16.f);
        bg->setPosition({size.width / 2 - m_barW / 2 - 2.f, size.height / 2 - 6.f});
        m_mainLayer->addChild(bg);
        m_fill = CCLayerColor::create({70, 220, 255, 255}, 1.f, 12.f);
        m_fill->setPosition({size.width / 2 - m_barW / 2, size.height / 2 - 4.f});
        m_mainLayer->addChild(m_fill);

        m_detail = CCLabelBMFont::create("", "chatFont.fnt");
        m_detail->setScale(0.5f);
        m_detail->setPosition({size.width / 2, size.height / 2 - 28.f});
        m_mainLayer->addChild(m_detail);

        auto* menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_mainLayer->addChild(menu);
        textButton(menu, "Cancel", {size.width / 2, 28.f}, 0.6f, [this] { requestCancel(); });
        this->scheduleUpdate();
        return true;
    }

    void requestCancel() {
        if (!scan::active()) { Popup::onClose(nullptr); return; }
        m_cancelling = true;
        scan::cancel();
        m_status->setString("Cancelling...");
    }

    void onClose(CCObject* sender) override {
        if (scan::active()) { requestCancel(); return; }
        Popup::onClose(sender);
    }

    void update(float) override {
        if (scan::active()) {
            bool over = scan::drive();
            if (!over) {
                auto p = scan::progress();
                float f = std::clamp(p.fraction(), 0.f, 1.f);
                m_fill->setContentSize({std::max(1.f, m_barW * f), 12.f});
                if (!m_cancelling) {
                    m_status->setString(p.phase == ScanPhase::Prepass ? "Replaying your run..." : "Measuring every click...");
                }
                m_detail->setString(fmt::format("{:.0f}%   click {}/{}   {} ticks simulated", f * 100.f, p.clicksDone, p.clicksTotal, p.steps).c_str());
                return;
            }
        }
        // finished (or never active): show the outcome and close
        this->unscheduleUpdate();
        auto const& r = scan::result();
        auto title = r.title.empty() ? std::string("Scan") : r.title;
        auto text = r.message;
        Popup::onClose(nullptr);
        if (!text.empty()) alertBox(title.c_str(), text);
    }

    ~ScanPopup() override {
        if (scan::active()) scan::abortNow();
    }

public:
    static ScanPopup* create() {
        auto* r = new (std::nothrow) ScanPopup();
        if (r && r->setup()) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }
};

void launch(bool ignoreMismatch) {
    auto* pl = PlayLayer::get();
    auto st = scan::start(pl, ignoreMismatch);
    if (!st.ok()) {
        alertBox("Cannot scan", st.text);
        return;
    }
    if (auto* p = ScanPopup::create()) p->show();
    else scan::abortNow();
}

}  // namespace

void startScanFlow() {
    auto* pl = PlayLayer::get();
    auto st = scan::check(pl, false);
    if (st.ok()) { launch(false); return; }
    if (st.error == scan::StartError::LevelMismatch) {
        createQuickPopup("Different level", st.text + "\n\nScan anyway? The result will most likely be meaningless.",
                         "Cancel", "Scan anyway", [](FLAlertLayer*, bool yes) { if (yes) launch(true); });
        return;
    }
    alertBox("Cannot scan", st.text);
}

}  // namespace afwc::ui
