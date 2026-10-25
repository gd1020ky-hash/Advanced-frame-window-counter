#include "GameSim.hpp"
#include "Shared.hpp"

using namespace geode::prelude;

namespace afwc::hook {

Flags& flags() {
    static Flags f;
    return f;
}

StepState& stepState() {
    static StepState s;
    return s;
}

GameSim::~GameSim() = default;

int32_t GameSim::tick() const { return int32_t(m_pl->m_gameState.m_currentProgress); }

bool GameSim::resetToStart() {
    auto& f = flags();
    f.dead = f.finished = false;
    m_pl->resetLevelFromStart();
    m_pl->m_queuedButtons.clear();
    m_pl->m_resumeTimer = 0;
    if (!m_pl->m_started) m_pl->startGame();
    f.dead = f.finished = false;   // a reset can emit a stale death callback
    return true;
}

void GameSim::input(bool down, bool p2) {
    // Only act when the engine's own button state differs. Recorded events are transitions, so this never skips a
    // real input; it only keeps the replay from pressing twice if the engine already mirrored the press to player 2
    // on its own (dual mode).
    PlayerObject* p = p2 ? m_pl->m_player2 : m_pl->m_player1;
    if (!p || holding(p) == down) return;
    auto& f = flags();
    f.injecting = true;
    m_pl->handleButton(down, 1, !p2);
    f.injecting = false;
}

bool GameSim::step() {
    auto& f = flags();
    int32_t before = tick();
    m_pl->m_queuedButtons.clear();   // the player's real touches must never leak into a scan
    m_pl->m_resumeTimer = 0;
    // One call of update() with exactly one physics step worth of time. The engine's time accumulator is emptied
    // first so that the number of steps can never depend on rounding left over from earlier calls. If the engine
    // still did not step (float rounding of the delta), the same call is repeated with a slightly larger delta:
    // a call that did nothing changed nothing. More than one step in a call is reported as a failure.
    static const double kScale[3] = {1.0, 1.0001, 1.001};
    f.stepping = true;
    for (int attempt = 0; attempt < 3 && tick() == before; attempt++) {
        m_pl->m_extraDelta = 0;
        m_pl->GJBaseGameLayer::update(float(m_dt * kScale[attempt]));
    }
    f.stepping = false;
    return tick() == before + 1;
}

bool GameSim::dead() const { return flags().dead; }
bool GameSim::finished() const { return flags().finished; }

SimPos GameSim::pos() const {
    SimPos p;
    if (auto* a = m_pl->m_player1) {
        auto q = a->getPosition();
        p.x1 = q.x;
        p.y1 = q.y;
        p.vy1 = a->m_yVelocity;
    }
    if (auto* b = m_pl->m_player2) {
        auto q = b->getPosition();
        p.x2 = q.x;
        p.y2 = q.y;
    }
    return p;
}

SimContext GameSim::context() const {
    SimContext c;
    if (auto* a = m_pl->m_player1) {
        auto q = a->getPosition();
        c.x = q.x;
        c.y = q.y;
        c.mode = a->m_isShip ? Mode::Ship : a->m_isBall ? Mode::Ball : a->m_isBird ? Mode::Ufo : a->m_isDart ? Mode::Wave
               : a->m_isRobot ? Mode::Robot : a->m_isSpider ? Mode::Spider : a->m_isSwing ? Mode::Swing : Mode::Cube;
    }
    c.percent = m_pl->getCurrentPercent();
    return c;
}

int GameSim::snapshot() {
    CheckpointObject* cp = m_pl->createCheckpoint();
    if (!cp) return -1;
    for (size_t i = 0; i < m_snaps.size(); i++) {
        if (!m_snaps[i]) { m_snaps[i] = cp; return int(i); }   // reuse a free slot
    }
    m_snaps.push_back(cp);
    return int(m_snaps.size()) - 1;
}

bool GameSim::restore(int h) {
    if (h < 0 || h >= int(m_snaps.size()) || !m_snaps[h]) return false;
    auto& f = flags();
    m_pl->loadFromCheckpoint(m_snaps[h]);
    m_pl->m_queuedButtons.clear();
    f.dead = f.finished = false;
    return true;
}

void GameSim::release(int h) {
    if (h >= 0 && h < int(m_snaps.size())) m_snaps[h] = nullptr;
}

bool GameSim::holding(PlayerObject* p) const {
    if (!p) return false;
    auto it = p->m_holdingButtons.find(1);
    return it != p->m_holdingButtons.end() && it->second;
}

void GameSim::syncHold(bool h1, bool h2) {
    auto& f = flags();
    f.injecting = true;
    if (holding(m_pl->m_player1) != h1) m_pl->handleButton(h1, 1, true);
    if (m_pl->m_player2 && holding(m_pl->m_player2) != h2) m_pl->handleButton(h2, 1, false);
    f.injecting = false;
}

}  // namespace afwc::hook
