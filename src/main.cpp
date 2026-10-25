// AFWC - Advanced Frame Window Counter.  Mod entry point.
#include "app/App.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    // Any change of a setting makes the HUD rebuild on the next frame.
    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) { afwc::app().revision++; });
}
