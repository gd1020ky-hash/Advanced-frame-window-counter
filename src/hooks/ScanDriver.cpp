#include "ScanDriver.hpp"
#include "../app/App.hpp"
#include "GameSim.hpp"
#include "Shared.hpp"
#include <chrono>

using namespace geode::prelude;

namespace afwc::scan {

namespace {

struct State {
    std::unique_ptr<hook::GameSim> sim;
    PlayLayer* pl = nullptr;
    bool wasPaused = false;
    bool objVisible = true;
    bool muted = false;
    Result result;
    ScanProgress last;
};

State& st() {
    static State s;
    return s;
}

// Silences the whole game while the scanner replays the level thousands of times (click / death sounds).
void setMasterMute(bool mute) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return;
    FMOD::ChannelGroup* master = nullptr;
    if (engine->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) master->setMute(mute);
}

int levelIdOf(PlayLayer* pl) { return pl && pl->m_level ? int(pl->m_level->m_levelID.value()) : 0; }

// Gives the game back: flags, visibility, mute, and (if the layer is still alive) a clean restart of the level.
void teardown(bool touchGame) {
    auto& a = app();
    auto& s = st();
    if (a.job && s.sim) a.job->cleanup(*s.sim);
    hook::flags() = hook::Flags{};
    auto* live = PlayLayer::get();
    bool alive = touchGame && live && live == s.pl;
    if (alive) {
        live->m_isPaused = s.wasPaused;
        if (live->m_objectLayer) live->m_objectLayer->setVisible(s.objVisible);
    }
    if (s.muted) setMasterMute(false);
    s.muted = false;
    a.job.reset();
    s.sim.reset();    // releases every snapshot
    clearLive();
    if (alive) live->resetLevelFromStart();
    s.pl = nullptr;
}

std::string summary(Run const& r) {
    int measured = 0, unverified = 0, unreachable = 0, capped = 0, active = 0;
    for (auto const& c : r.clicks) {
        if (c.shouldDraw) active++;
        if (c.flags & CF_Unreachable) unreachable++;
        else if (c.scanned() && !c.verified()) unverified++;
        else if (c.measured()) {
            measured++;
            if (c.flags & (CF_EarlyCapped | CF_LateCapped)) capped++;
        }
    }
    std::string head;
    if (r.replayCheck == RC_Match) {
        head = "The re-simulation <cg>reproduced your attempt exactly</c>";
        if (r.alignShift != 0) head += fmt::format(" after moving every input by {:+d} tick(s)", r.alignShift);
        head += ".";
    } else if (r.replayCheck == RC_Mismatch) {
        head = "<cr>The re-simulation did NOT reproduce how your attempt ended.</c> The windows describe the replay, "
               "which may differ from the run you actually played.";
    } else {
        head = "How the original attempt ended was not known, so the replay could not be compared with it.";
    }
    std::string s = head + fmt::format("\n\n<cy>{}</c> of {} clicks measured", measured, active);
    if (capped) s += fmt::format(", {} of them are lower bounds (search radius or neighbouring click)", capped);
    if (unverified) s += fmt::format("\n<co>{}</c> clicks could not be verified (the game was not deterministic there)", unverified);
    if (unreachable) s += fmt::format("\n{} clicks lie after the end of the replay and were skipped", unreachable);
    return s;
}

void finish() {
    auto& a = app();
    auto& s = st();
    Result res;
    res.phase = a.job->phase();
    s.last = a.job->progress();
    if (res.phase == ScanPhase::Done) {
        Run r = a.job->takeResult();
        res.title = "Scan finished";
        res.message = summary(r);
        setActiveRun(std::move(r), true);
        res.applied = true;
    } else if (res.phase == ScanPhase::Failed) {
        res.title = "Scan failed";
        res.message = s.last.error.empty() ? std::string("Unknown error.") : s.last.error;
        log::warn("AFWC scan failed: {}", res.message);
    } else {
        res.title = "Scan cancelled";
        res.message = "Nothing was changed.";
    }
    teardown(true);
    s.result = std::move(res);
}

}  // namespace

bool active() { return app().job != nullptr; }

StartStatus check(PlayLayer* pl, bool ignoreLevelMismatch) {
    auto& a = app();
    if (a.job) return {StartError::Busy, "A scan is already running."};
    if (!pl || !pl->m_level) return {StartError::NoLevel, "Open the level first: the scan re-plays the run inside the level."};
    if (!a.hasRun || a.run.clicks.empty()) return {StartError::NoRun, "There is no run to scan. Play the level, then open AFWC from the pause menu and press Use attempt (or finish the level)."};
    if (!a.run.scannable || a.run.events.empty())
        return {StartError::NotScannable,
                "This run has no recorded presses/releases (or you used platformer movement buttons, which cannot be "
                "replayed through the jump button), so it cannot be re-simulated."};
    int lid = levelIdOf(pl);
    if (!ignoreLevelMismatch && a.run.levelID != 0 && lid != 0 && a.run.levelID != lid)
        return {StartError::LevelMismatch,
                fmt::format("This run was recorded in level {} ({}) but you are in level {}.", a.run.levelID,
                            a.run.levelName.empty() ? std::string("?") : a.run.levelName, lid)};
    return {};
}

StartStatus start(PlayLayer* pl, bool ignoreLevelMismatch) {
    auto st0 = check(pl, ignoreLevelMismatch);
    if (!st0.ok()) return st0;
    auto& a = app();
    auto& s = st();
    a.job = std::make_unique<ScanJob>(a.run, scanConfigFromSettings());
    s.sim = std::make_unique<hook::GameSim>(pl, a.run.tps);
    s.pl = pl;
    s.wasPaused = pl->m_isPaused;
    s.objVisible = pl->m_objectLayer ? pl->m_objectLayer->isVisible() : true;
    s.result = Result{};
    s.last = ScanProgress{};
    pl->m_isPaused = false;
    if (pl->m_objectLayer) pl->m_objectLayer->setVisible(false);
    if (opt<bool>("scan-mute")) {
        setMasterMute(true);
        s.muted = true;
    }
    hook::flags() = hook::Flags{};
    hook::flags().scanActive = true;
    log::info("AFWC scan started: {} clicks, {} events, level {}", a.run.clicks.size(), a.run.events.size(), levelIdOf(pl));
    return {};
}

bool drive() {
    auto& a = app();
    auto& s = st();
    if (!a.job) return true;
    auto* pl = PlayLayer::get();
    if (!pl || pl != s.pl) {
        // the level went away under us
        Result res;
        res.phase = ScanPhase::Failed;
        res.title = "Scan stopped";
        res.message = "The level was closed while the scan was running.";
        teardown(false);
        s.result = std::move(res);
        return true;
    }
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    auto budget = std::chrono::milliseconds(std::max<int64_t>(2, opt<int64_t>("scan-budget-ms")));
    bool done = false;
    do {
        done = a.job->advance(*s.sim, 25);
    } while (!done && clock::now() - t0 < budget);
    if (!done) {
        s.last = a.job->progress();
        return false;
    }
    finish();
    return true;
}

void cancel() {
    if (app().job) app().job->cancel();
}

void abortNow(bool restartLevel) {
    auto& a = app();
    auto& s = st();
    if (!a.job) return;
    teardown(restartLevel);
    s.result = Result{ScanPhase::Cancelled, "Scan cancelled", "Nothing was changed.", false};
}

ScanProgress progress() {
    auto& a = app();
    return a.job ? a.job->progress() : st().last;
}

Result const& result() { return st().result; }

}  // namespace afwc::scan
