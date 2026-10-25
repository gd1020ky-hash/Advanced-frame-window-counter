// AFWC - game hooks: records the attempt, blocks the real game while a scan runs, drives the overlay.
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "../app/App.hpp"
#include "Overlay.hpp"
#include "ScanDriver.hpp"
#include "Shared.hpp"

using namespace geode::prelude;
using namespace afwc;

namespace {

bool isLive(GJBaseGameLayer const* g) {
    auto* pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer const*>(pl) == g && opt<bool>("enabled");
}

bool recording(GJBaseGameLayer const* g) {
    return isLive(g) && !afwc::hook::flags().scanActive && opt<bool>("auto-record");
}

// tick of an input the engine processes right now (see afwc::hook::StepState)
int32_t tickNow(GJBaseGameLayer* g) {
    auto& s = afwc::hook::stepState();
    return s.inUpdate ? s.base + s.steps : int32_t(g->m_gameState.m_currentProgress);
}

Mode modeOf(PlayerObject* a) {
    if (!a) return Mode::Unknown;
    return a->m_isShip ? Mode::Ship : a->m_isBall ? Mode::Ball : a->m_isBird ? Mode::Ufo : a->m_isDart ? Mode::Wave
         : a->m_isRobot ? Mode::Robot : a->m_isSpider ? Mode::Spider : a->m_isSwing ? Mode::Swing : Mode::Cube;
}

bool holding(PlayerObject* p) {
    if (!p) return false;
    auto it = p->m_holdingButtons.find(1);
    return it != p->m_holdingButtons.end() && it->second;
}

// The level jumped back to `cur` (restart, checkpoint, practice respawn): forget everything recorded after it and
// make the recorded button state agree with the engine's.
void syncAfterRewind(PlayLayer* pl) {
    auto& a = app();
    int32_t cur = int32_t(pl->m_gameState.m_currentProgress);
    std::erase_if(a.live, [&](InputEvent const& e) { return e.tick >= cur; });
    for (auto it = a.liveCtx.begin(); it != a.liveCtx.end();) {
        if (it->first.first >= cur) it = a.liveCtx.erase(it); else ++it;
    }
    for (int p = 0; p < 2; p++) {
        a.liveHeld[p] = heldBefore(a.live, cur, p == 1);
        PlayerObject* po = p ? pl->m_player2 : pl->m_player1;
        if (a.liveHeld[p] && !holding(po)) {
            a.live.push_back({cur, false, p == 1});   // the engine let go of the button on the rewind
            a.liveHeld[p] = false;
        }
    }
    if (cur == 0) a.liveUnsupported = false;
    a.liveTick = cur;
    overlay::rewound(pl, cur);
}

}  // namespace

class $modify(AfwcBaseLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto* pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != static_cast<GJBaseGameLayer*>(this)) {
            GJBaseGameLayer::update(dt);
            return;
        }
        auto& f = afwc::hook::flags();
        if (f.scanActive && !f.stepping) return;   // the scan owns the game; real frames are dropped
        auto& s = afwc::hook::stepState();
        bool outer = !s.inUpdate;
        if (outer) {
            s.inUpdate = true;
            s.base = int32_t(m_gameState.m_currentProgress);
            s.steps = 0;
        }
        GJBaseGameLayer::update(dt);
        if (outer) {
            s.inUpdate = false;
            if (!f.scanActive) app().liveTick = int32_t(m_gameState.m_currentProgress);
        }
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto& s = afwc::hook::stepState();
        if (s.inUpdate && !isHalfTick) s.steps++;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (recording(this) && !afwc::hook::flags().injecting && m_started) {
            auto& a = app();
            if (button != 1) {
                a.liveUnsupported = true;      // platformer movement cannot be replayed through the jump button
            } else {
                bool p2 = !isPlayer1;
                int pi = p2 ? 1 : 0;
                if (a.liveHeld[pi] != down) {
                    a.liveHeld[pi] = down;
                    int32_t t = tickNow(this);
                    a.live.push_back({t, down, p2});
                    if (down) {
                        LiveCtx c;
                        PlayerObject* po = p2 && m_player2 ? m_player2 : m_player1;
                        if (po) { auto q = po->getPosition(); c.x = q.x; c.y = q.y; }
                        c.mode = modeOf(po);
                        if (auto* pl = PlayLayer::get()) c.percent = pl->getCurrentPercent();
                        a.liveCtx[{t, p2}] = c;
                    }
                }
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(AfwcPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        afwc::hook::flags() = afwc::hook::Flags{};
        afwc::hook::stepState() = afwc::hook::StepState{};
        clearLive();
        auto& a = app();
        int id = level ? int(level->m_levelID.value()) : 0;
        if (a.hasRun && a.run.levelID != 0 && a.run.levelID != id) {
            if (!loadRunForLevel(id)) clearActiveRun();
        } else if (!a.hasRun) {
            loadRunForLevel(id);
        }
        a.hasLastAttempt = false;
        overlay::attach(this);
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (afwc::hook::flags().scanActive) return;
        overlay::frame(this, int32_t(m_gameState.m_currentProgress), dt);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (afwc::hook::flags().scanActive || !isLive(this)) return;
        syncAfterRewind(this);
    }

    void loadFromCheckpoint(CheckpointObject* cp) {
        PlayLayer::loadFromCheckpoint(cp);
        if (afwc::hook::flags().scanActive || !isLive(this)) return;
        syncAfterRewind(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& f = afwc::hook::flags();
        if (f.scanActive) {
            if (!f.finished) f.dead = true;    // the scan decides what a death means; the real one never happens
            return;
        }
        if (recording(this) && m_player1 && !m_player1->m_isDead && !m_player2Dead()) {
            auto& s = afwc::hook::stepState();
            int32_t end = s.inUpdate ? s.base + s.steps + 1 : int32_t(m_gameState.m_currentProgress);
            auto& a = app();
            if (!a.live.empty()) {
                Run r = runFromLive(m_level, OUT_Died, end);
                if (!r.clicks.empty()) {
                    a.lastAttempt = std::move(r);
                    a.hasLastAttempt = true;
                }
            }
        }
        PlayLayer::destroyPlayer(player, object);
    }

    bool m_player2Dead() { return m_player2 && m_player2->m_isDead; }

    void levelComplete() {
        auto& f = afwc::hook::flags();
        if (f.scanActive) {
            if (!f.dead) f.finished = true;
            return;
        }
        if (recording(this) && !m_isTestMode) {
            auto& a = app();
            if (opt<bool>("auto-save-complete") && !a.live.empty()) {
                Run r = runFromLive(m_level, OUT_Completed, tickNow(this));
                if (!r.clicks.empty()) {
                    int n = int(r.clicks.size());
                    setActiveRun(std::move(r), true);
                    notify(fmt::format("AFWC: run saved ({} clicks). Pause the level and open AFWC to scan it.", n));
                }
            }
        }
        PlayLayer::levelComplete();
    }

    // The end of the level can also be signalled through the end animation; during a scan either one means "finished".
    void playEndAnimationToPos(CCPoint position) {
        auto& f = afwc::hook::flags();
        if (f.scanActive) {
            if (!f.dead) f.finished = true;
            return;
        }
        PlayLayer::playEndAnimationToPos(position);
    }

    void pauseGame(bool unfocused) {
        if (afwc::hook::flags().scanActive) return;    // never pause underneath a running scan
        PlayLayer::pauseGame(unfocused);
    }

    void onQuit() {
        scan::abortNow(false);
        overlay::detach();
        afwc::hook::flags() = afwc::hook::Flags{};
        clearLive();
        PlayLayer::onQuit();
    }
};
