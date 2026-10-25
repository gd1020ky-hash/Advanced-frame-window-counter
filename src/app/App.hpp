// AFWC - application state shared by hooks and UI.
#pragma once
#include <Geode/Geode.hpp>
#include "../core/Presets.hpp"
#include "../core/RunBuilder.hpp"
#include "../core/Scanner.hpp"
#include <functional>
#include <map>
#include <memory>

namespace afwc {

// Quick numbers about the active run, shown in the panel.
struct Summary {
    int total = 0, active = 0, withWindow = 0, measured = 0, capped = 0, unverified = 0, unreachable = 0;
    double minWindow = 0, medianWindow = 0;
};

struct LiveCtx {
    float x = 0, y = 0, percent = 0;
    Mode mode = Mode::Unknown;
};

struct App {
    // ---- active run (what the HUD, markers, statistics and the panel show) ----
    Run run;
    bool hasRun = false;
    uint32_t revision = 0;          // bumped whenever the run / presets change so the HUD can rebuild
    Summary summary;

    // ---- the attempt that is currently being recorded ----
    std::vector<InputEvent> live;
    std::map<std::pair<int32_t, bool>, LiveCtx> liveCtx;   // press context keyed by (tick, player2)
    int32_t liveTick = 0;
    bool liveHeld[2] = {false, false};   // button state of the recorded timeline (player 1 / 2)
    bool liveUnsupported = false;        // an input AFWC cannot replay was used (platformer movement buttons)
    Run lastAttempt;                     // the attempt that ended with the last death
    bool hasLastAttempt = false;

    // ---- HUD counter labels (1F, 2F, 3F, 4-6F, 7F+) ----
    std::vector<LabelPreset> labels = defaultLabelPresets();

    // ---- scanner ----
    std::unique_ptr<ScanJob> job;
    bool scanning() const { return job != nullptr; }
};

App& app();

// settings ------------------------------------------------------------------------------------------------------
template <class T> inline T opt(char const* key) { return geode::Mod::get()->getSettingValue<T>(key); }
ScanConfig scanConfigFromSettings();

// run management --------------------------------------------------------------------------------------------------
void setActiveRun(Run run, bool persist);
void touchRun(bool persist);                    // after changing clicks in place
void recomputeSummary();
// snapshot of the attempt being recorded; `outcome` is a RunOutcome, `endTick` the tick counter after the last step
Run runFromLive(GJGameLevel* level, uint8_t outcome, int32_t endTick);
void clearLive();                               // forget the recorded attempt (new attempt / rewind to the start)
void clearActiveRun();
std::filesystem::path runsDir();
bool saveRunFile(Run const& r, std::filesystem::path const& path, std::string* err = nullptr);
bool loadRunFile(std::filesystem::path const& path, Run& out, std::string* err = nullptr);
bool loadRunForLevel(int levelID);
std::string describeRun(Run const& r);

// HUD labels -------------------------------------------------------------------------------------------------------------
// marker look for a click: colour and text from the first label that matches its window
void markerLook(Click const& c, Color4& color, std::string& text);

void notify(std::string const& msg, bool error = false);

}  // namespace afwc
