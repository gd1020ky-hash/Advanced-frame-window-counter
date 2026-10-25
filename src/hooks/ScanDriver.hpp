// AFWC - owns the scan while it runs: takes over the PlayLayer, drives the time-sliced ScanJob and hands the game
// back afterwards. The UI (ScanPopup) only calls start / drive / cancel and reads progress / result.
#pragma once
#include "../core/Scanner.hpp"
#include <Geode/Geode.hpp>
#include <string>

namespace afwc::scan {

enum class StartError : uint8_t { None, Busy, NoLevel, NoRun, NotScannable, LevelMismatch };

struct StartStatus {
    StartError error = StartError::None;
    std::string text;    // human readable reason (Geode markup allowed)
    bool ok() const { return error == StartError::None; }
};

struct Result {
    ScanPhase phase = ScanPhase::Idle;
    std::string title;
    std::string message;
    bool applied = false;   // the scan result replaced the active run
};

bool active();

// Can a scan start right now? (no side effects)
StartStatus check(PlayLayer* pl, bool ignoreLevelMismatch);
// Takes over the game and creates the job. On failure nothing was changed.
StartStatus start(PlayLayer* pl, bool ignoreLevelMismatch);
// Time-sliced work, call once per rendered frame. Returns true once the scan is over (result() is then valid).
bool drive();
// Ask the job to stop; drive() then performs the teardown.
void cancel();
// Immediate teardown (level closed, popup destroyed). Safe to call at any time.
void abortNow(bool restartLevel = true);

ScanProgress progress();
Result const& result();

}  // namespace afwc::scan
