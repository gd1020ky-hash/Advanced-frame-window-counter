// AFWC - entry points into the mod menu: a button in the pause menu, a button on the main menu and (desktop) the O key.
#include "../app/App.hpp"
#include "../ui/Ui.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;

namespace {

// The AFWC button: our own icon, or a stock GD icon if the file is missing.
CCMenuItemSpriteExtra* makeButton() {
    CCSprite* icon = CCSprite::create("afwc_icon.png"_spr);
    if (!icon) icon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
    if (!icon) return nullptr;
    icon->setScale(icon->getContentWidth() > 60.f ? 40.f / icon->getContentWidth() : 1.f);
    auto* btn = geode::cocos::CCMenuItemExt::createSpriteExtra(icon, [](CCMenuItemSpriteExtra*) { afwc::ui::openMenu(); });
    btn->setID("afwc-button"_spr);
    return btn;
}

}  // namespace

class $modify(AfwcPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto* btn = makeButton();
        if (!btn) return;
        if (auto* menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto* m = CCMenu::create();
            m->setID("afwc-menu"_spr);
            auto win = CCDirector::get()->getWinSize();
            m->setPosition({win.width - 32.f, 90.f});
            m->addChild(btn);
            this->addChild(m);
        }
    }
};

class $modify(AfwcMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (auto* menu = this->getChildByID("bottom-menu")) {
            if (auto* btn = makeButton()) {
                menu->addChild(btn);
                menu->updateLayout();
            }
        }
        return true;
    }
};

#ifndef GEODE_IS_MOBILE
$execute {
    // O opens / closes the menu (not while a text field has the keyboard).
    KeyboardInputEvent(KEY_O).listen([](KeyboardInputData& d) -> bool {
        if (d.action != KeyboardInputData::Action::Press) return false;
        if (d.modifiers != KeyboardModifier::None) return false;
        if (CCIMEDispatcher::sharedDispatcher()->hasDelegate()) return false;
        // inside a level the menu only opens from the pause menu, never in the middle of an attempt
        if (auto* pl = PlayLayer::get(); pl && !pl->m_isPaused) return false;
        if (!afwc::app().scanning()) afwc::ui::openMenu();
        return false;
    }).leak();
}
#endif
  
