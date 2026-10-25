// AFWC - user interface entry points and small helpers shared by the popups.
#pragma once
#include <Geode/Geode.hpp>
#include <functional>

namespace afwc::ui {

void openMenu();              // the AFWC mod menu (actions + options), or closes it when it is already open
void openPanel();             // run details: header, check result and the click list; closes it when already open
void startScanFlow();         // checks the run / level, then opens the scan popup
void showHelp();              // the how-to popup
void useAttempt();            // make the last attempt (or the one being played) the active run

// A text button in the AFWC style.
inline CCMenuItemSpriteExtra* textButton(cocos2d::CCMenu* menu, char const* text, cocos2d::CCPoint pos, float scale,
                                         std::function<void()> cb) {
    auto* spr = ButtonSprite::create(text);
    spr->setScale(scale);
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(cb)](CCMenuItemSpriteExtra*) { cb(); });
    item->setPosition(pos);
    menu->addChild(item);
    return item;
}

inline void alertBox(char const* title, std::string const& text) {
    FLAlertLayer::create(title, text, "OK")->show();
}

}  // namespace afwc::ui
