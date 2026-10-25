#include "Scanner.hpp"
#include <algorithm>
#include <climits>

namespace afwc {

float ScanProgress::fraction() const {
    switch (phase) {
        case ScanPhase::Done: return 1.f;
        case ScanPhase::Prepass:
            return endTick > 0 ? 0.10f * std::min(1.f, float(tick) / float(endTick)) : 0.f;
        case ScanPhase::Trials:
            return 0.10f + 0.90f * (clicksTotal > 0 ? float(clicksDone) / float(clicksTotal) : 1.f);
        default: return 0.f;
    }
}

ScanJob::ScanJob(Run run, ScanConfig cfg) : run_(std::move(run)), cfg_(cfg) {
    // make sure clicks and events agree (clicks are always derived from events)
    Run tmp = run_;
    deriveClicks(tmp);
    bool same = tmp.clicks.size() == run_.clicks.size();
    for (size_t i = 0; same && i < tmp.clicks.size(); i++)
        same = tmp.clicks[i].tick == run_.clicks[i].tick && tmp.clicks[i].p2 == run_.clicks[i].p2;
    sortEvents(run_.events);
    if (!same) run_.clicks = tmp.clicks;
    for (size_t i = 0; i < run_.clicks.size(); i++) run_.clicks[i].release = tmp.clicks[i].release;
    cfg_.maxShift = std::max(1, cfg_.maxShift);
    lastEventTick_ = run_.events.empty() ? 0 : run_.events.back().tick;
    plans_.assign(run_.clicks.size(), Plan{});
    // pair press/release event indices
    int open[2] = {-1, -1};
    int ci = 0;
    for (size_t ei = 0; ei < run_.events.size(); ei++) {
        auto const& e = run_.events[ei];
        int p = e.p2 ? 1 : 0;
        if (e.down) {
            if (open[p] >= 0) continue;
            plans_[ci].pressEv = (int)ei;
            open[p] = ci++;
        } else if (open[p] >= 0) {
            plans_[open[p]].relEv = (int)ei;
            open[p] = -1;
        }
    }
    for (auto const& e : run_.events) origEvTick_.push_back(e.tick);
    for (auto const& k : run_.clicks) { origClTick_.push_back(k.tick); origClRel_.push_back(k.release); }
    phase_ = ScanPhase::Prepass;
    stage_ = Stage::Init;
}

ScanJob::~ScanJob() = default;

namespace {
const int kAlign[4] = {0, -1, 1, 0};   // input-alignment candidates: as recorded, one tick earlier, one tick later, back to as recorded
}

// Ground truth: how did the attempt that was really played end, and did the reference pass end the same way?
bool ScanJob::outcomeOk() const {
    if (!cfg_.checkOutcome || run_.outcome == OUT_Unknown) return true;   // nothing to compare against
    if (run_.outcome == OUT_Completed) return refCompleted_;
    int64_t diff = int64_t(refEnd_) - int64_t(run_.endTick);
    if (diff < 0) diff = -diff;
    return refDied_ && diff <= cfg_.deathTolerance;
}

int32_t ScanJob::prepassLimit() const {
    int32_t limit = lastEventTick_ + cfg_.tailTicks;
    // when the real outcome is known the reference pass must run at least that far to be comparable
    if (cfg_.checkOutcome && run_.outcome != OUT_Unknown) limit = std::max<int32_t>(limit, run_.endTick + cfg_.deathTolerance + 2);
    return limit;
}

void ScanJob::setShift(int s) {
    for (size_t i = 0; i < run_.events.size() && i < origEvTick_.size(); i++)
        run_.events[i].tick = std::max<int32_t>(0, origEvTick_[i] + s);
    for (size_t i = 0; i < run_.clicks.size() && i < origClTick_.size(); i++) {
        run_.clicks[i].tick = std::max<int32_t>(0, origClTick_[i] + s);
        run_.clicks[i].release = origClRel_[i] >= 0 ? std::max<int32_t>(0, origClRel_[i] + s) : -1;
    }
    appliedShift_ = s;
    lastEventTick_ = run_.events.empty() ? 0 : run_.events.back().tick;
}

ScanProgress ScanJob::progress() const {
    ScanProgress p;
    p.phase = phase_;
    p.clicksDone = clicksDone_;
    int total = 0;
    for (auto const& pl : plans_) total += pl.unreachable ? 0 : 1;
    p.clicksTotal = phase_ == ScanPhase::Prepass ? int(plans_.size()) : total;
    p.steps = steps_;
    p.restores = restores_;
    p.error = error_;
    p.endTick = phase_ == ScanPhase::Prepass ? prepassLimit() : refEnd_;
    p.tick = ref_.empty() ? 0 : int32_t(ref_.size()) - 1;
    return p;
}

void ScanJob::cleanup(ISim& sim) {
    if (snap_ >= 0) { sim.release(snap_); snap_ = -1; }
    if (snap0_ >= 0) { sim.release(snap0_); snap0_ = -1; }
}

bool ScanJob::fail(ISim& sim, std::string msg) {
    error_ = std::move(msg);
    phase_ = ScanPhase::Failed;
    stage_ = Stage::Over;
    cleanup(sim);
    return true;
}

void ScanJob::applyBaseEvents(ISim& sim, int32_t tick, size_t& pos) {
    auto& ev = run_.events;
    while (pos < ev.size() && ev[pos].tick < tick) pos++;
    while (pos < ev.size() && ev[pos].tick == tick) {
        sim.input(ev[pos].down, ev[pos].p2);
        pos++;
    }
}

void ScanJob::buildPlans() {
    auto& cl = run_.clicks;
    const int W = cfg_.maxShift;
    const int INF = INT_MAX / 4;
    for (size_t k = 0; k < cl.size(); k++) {
        Plan& p = plans_[k];
        int prev = -1, next = -1;
        for (int j = int(k) - 1; j >= 0; j--) if (cl[j].p2 == cl[k].p2) { prev = j; break; }
        for (size_t j = k + 1; j < cl.size(); j++) if (cl[j].p2 == cl[k].p2) { next = int(j); break; }
        int32_t t = cl[k].tick, r = cl[k].release;
        int32_t lowestPress = prev >= 0 ? (cl[prev].release >= 0 ? cl[prev].release + 1 : cl[prev].tick + 1) : 0;
        int dMinAdj = int(lowestPress - t);
        p.dMin = std::min(0, std::max(-W, dMinAdj));
        p.earlyByAdjacency = dMinAdj > -W;
        int dMaxAdj = INF;
        if (cfg_.shiftMode == ShiftMode::WholeTap) {
            if (next >= 0) dMaxAdj = (r >= 0) ? int(cl[next].tick - 1 - r) : int(cl[next].tick - 1 - t);
        } else {
            if (r >= 0) dMaxAdj = int(r - 1 - t);
            else if (next >= 0) dMaxAdj = int(cl[next].tick - 1 - t);
        }
        p.dMax = std::max(0, std::min(W, dMaxAdj));
        p.lateByAdjacency = dMaxAdj < W;
        p.snapTick = t + p.dMin;
        int64_t huncapped = INT64_MAX;
        if (next >= 0 && cfg_.horizonExtra >= 0) huncapped = int64_t(cl[next].tick) + cfg_.horizonExtra;
        p.horizon = int32_t(std::min<int64_t>(huncapped, refEnd_));
        p.unreachable = t >= refEnd_ || (refDied_ && huncapped >= refEnd_);
        // the horizon must be beyond every shifted event
        if (!p.unreachable) {
            int32_t need = t + p.dMax + 1;
            if (r >= 0 && cfg_.shiftMode == ShiftMode::WholeTap) need = std::max<int32_t>(need, r + p.dMax + 1);
            if (p.horizon < need) p.horizon = std::min<int32_t>(need, refEnd_);
        }
        if (p.unreachable) {
            cl[k].flags |= CF_Unreachable;
            cl[k].early = cl[k].late = -1;
        }
    }
    order_.clear();
    for (size_t k = 0; k < cl.size(); k++) if (!plans_[k].unreachable) order_.push_back(int(k));
    std::stable_sort(order_.begin(), order_.end(), [&](int a, int b) { return plans_[a].snapTick < plans_[b].snapTick; });
    orderPos_ = 0;
}

void ScanJob::setupTrial(ISim& sim, int k, int d) {
    Plan const& p = plans_[k];
    auto const& ev = run_.events;
    tev_.clear();
    // events are sorted; a shift moves an event by at most maxShift ticks, so nothing beyond this bound can matter
    const int64_t bound = int64_t(p.horizon) + cfg_.maxShift;
    for (size_t i = snapBasePos_; i < ev.size() && ev[i].tick <= bound; i++) {
        InputEvent e = ev[i];
        if ((int)i == p.pressEv) e.tick += d;
        else if (cfg_.shiftMode == ShiftMode::WholeTap && (int)i == p.relEv) e.tick += d;
        if (e.tick >= p.horizon) continue;  // the trial stops at the horizon, later inputs never apply
    clicksDone_++;
}

bool ScanJob::advance(ISim& sim, int maxSteps) {
    if (phase_ == ScanPhase::Done || phase_ == ScanPhase::Failed || phase_ == ScanPhase::Cancelled) return true;
    if (cancel_) {
        cleanup(sim);
        phase_ = ScanPhase::Cancelled;
        stage_ = Stage::Over;
        return true;
    }
    int budget = std::max(1, maxSteps);
    auto& ev = run_.events;
    auto& cl = run_.clicks;

    while (budget > 0) {
        switch (stage_) {
        case Stage::Init: {
            if (ev.empty() || cl.empty()) return fail(sim, "this run has no clicks to scan");
            if (!sim.resetToStart()) return fail(sim, "could not restart the level");
            if (sim.tick() != 0) return fail(sim, "level did not restart at tick 0");
            snap0_ = sim.snapshot();
            if (snap0_ < 0) return fail(sim, "could not create a game-state snapshot");
            ref_.clear();
            bpos_ = 0;
            ctxPos_ = 0;
            refDied_ = false;
            stage_ = Stage::Prepass;
            break;
        }
        case Stage::Prepass: {
            int32_t c = sim.tick();
            if (c != int32_t(ref_.size())) return fail(sim, "tick counter went out of sync during the reference pass");
            ref_.push_back(sim.pos());
            if (ctxPos_ < cl.size() && cl[ctxPos_].tick <= c) {
                SimContext ctx = sim.context();
                while (ctxPos_ < cl.size() && cl[ctxPos_].tick <= c) {
                    Click& k = cl[ctxPos_++];
                    if (k.mode == Mode::Unknown) { k.x = ctx.x; k.y = ctx.y; k.percent = ctx.percent; k.mode = ctx.mode; }
                }
            }
            applyBaseEvents(sim, c, bpos_);
            if (!sim.step()) return fail(sim, "the game did not advance by exactly one tick (time-warp / TPS mismatch?)");
            steps_++; budget--;
            bool end = false;
            if (sim.dead()) { refEnd_ = sim.tick(); refCompleted_ = false; refDied_ = true; end = true; }
            else if (sim.finished()) { refEnd_ = sim.tick(); refCompleted_ = true; end = true; }
            else if (sim.tick() >= prepassLimit()) { refEnd_ = sim.tick(); refCompleted_ = false; end = true; }
            else if (sim.tick() >= cfg_.hardLimitTicks) return fail(sim, "run is too long to scan");
            if (end) {
                ref_.push_back(sim.pos());
                if (!sim.restore(snap0_)) return fail(sim, "could not restore the start snapshot");
                restores_++;
                sim.syncHold(false, false);
                bpos_ = 0;
                bool ok = outcomeOk();
                if (!ok && cfg_.autoAlign && alignIdx_ < 3) {
                    // the replay does not end the way the real attempt ended: retry with the inputs moved by one tick
                    alignIdx_++;
                    setShift(kAlign[alignIdx_]);
                    ref_.clear();
                    ctxPos_ = 0;
                    refDied_ = false;
                    refCompleted_ = false;
                    refEnd_ = 0;
                    break;   // stay in the prepass
                }
                run_.replayCheck = (!cfg_.checkOutcome || run_.outcome == OUT_Unknown) ? RC_NotChecked : (ok ? RC_Match : RC_Mismatch);
                run_.alignShift = appliedShift_;
                buildPlans();
                clicksDone_ = 0;
                phase_ = ScanPhase::Trials;
                stage_ = Stage::Pick;
            }
            break;
        }
        case Stage::Pick: {
            if (orderPos_ >= order_.size()) { stage_ = Stage::Finish; break; }
            cur_ = order_[orderPos_++];
            Plan const& p = plans_[cur_];
            if (snap_ >= 0 && snapTick_ == p.snapTick) { stage_ = Stage::TrialSetup; side_ = Side::Baseline; d_ = 0; break; }
            if (snap_ >= 0) {
                if (!sim.restore(snap_)) return fail(sim, "could not restore a snapshot");
                restores_++;
                sim.syncHold(heldSnap1_, heldSnap2_);
                bpos_ = snapBasePos_;
                sim.release(snap_);
                snap_ = -1;
            }
            stage_ = Stage::Advance;
            break;
        }
        case Stage::Advance: {
            Plan const& p = plans_[cur_];
            if (sim.tick() < p.snapTick) {
                int32_t c = sim.tick();
                applyBaseEvents(sim, c, bpos_);
                if (!sim.step()) return fail(sim, "the game did not advance by exactly one tick");
                steps_++; budget--;
                int32_t nt = sim.tick();
                if (sim.dead() || sim.finished())
                    return fail(sim, "the replay ended early at tick " + std::to_string(nt) + " (the game is not deterministic here)");
                if (cfg_.verifyBaseline && nt < int32_t(ref_.size()) && !(sim.pos() == ref_[nt]))
                    return fail(sim, "re-simulation diverged from the original run at tick " + std::to_string(nt) +
                                         " (checkpoint restore is not exact for this level)");
                break;
            }
            snap_ = sim.snapshot();
            if (snap_ < 0) return fail(sim, "could not create a game-state snapshot");
            snapTick_ = p.snapTick;
            snapBasePos_ = bpos_;
            heldSnap1_ = heldBefore(ev, snapTick_, false);
            heldSnap2_ = heldBefore(ev, snapTick_, true);
            side_ = Side::Baseline;
            d_ = 0;
            stage_ = Stage::TrialSetup;
            break;
        }
        case Stage::TrialSetup: {
            if (side_ == Side::Baseline && d_ == 0) { lateOk_ = earlyOk_ = 0; baselineOk_ = false; lastPassed_ = false; }
            if (!sim.restore(snap_)) return fail(sim, "could not restore a snapshot");
            restores_++;
            sim.syncHold(heldSnap1_, heldSnap2_);
            if (sim.tick() != snapTick_) return fail(sim, "snapshot restore changed the tick counter");
            setupTrial(sim, cur_, d_);
            stage_ = Stage::Trial;
            break;
        }
        case Stage::Trial: {
            int32_t c = sim.tick();
            bool done = false, pass = false;
            if (c >= tHorizon_) { done = true; pass = true; }
            else {
                while (tpos_ < tev_.size() && tev_[tpos_].tick <= c) {
                    if (tev_[tpos_].tick == c) sim.input(tev_[tpos_].down, tev_[tpos_].p2);
                    tpos_++;
                }
                if (!sim.step()) return fail(sim, "the game did not advance by exactly one tick");
                steps_++; budget--;
                if (sim.dead()) { done = true; pass = false; }
                else if (sim.finished()) { done = true; pass = true; }
                else if (trialBaseline_ && sim.tick() < int32_t(ref_.size()) && !(sim.pos() == ref_[sim.tick()])) { done = true; pass = false; }
            }
            if (!done) break;
            lastPassed_ = pass;
            if (side_ == Side::Baseline) {
                baselineOk_ = pass;
                if (!pass) { finishClick(cur_); stage_ = Stage::Pick; break; }
            }
            if (nextShift(cur_)) { stage_ = Stage::TrialSetup; break; }
            finishClick(cur_);
            stage_ = Stage::Pick;
            break;
        }
        case Stage::Finish: {
            cleanup(sim);
            phase_ = ScanPhase::Done;
            stage_ = Stage::Over;
            return true;
        }
        case Stage::Over: return true;
        }
        if (cancel_) {
            cleanup(sim);
            phase_ = ScanPhase::Cancelled;
            stage_ = Stage::Over;
            return true;
        }
    }
    return false;
}

}  // namespace afwc
