#pragma once
#include "../core/Scanner.hpp"
#include <Geode/Geode.hpp>

namespace afwc::hook {

// ISim implemented on top of the real PlayLayer: exact 1-tick stepping, checkpoint-based snapshots.
class GameSim final : public ISim {
public:
    GameSim(PlayLayer* pl, double tps) : m_pl(pl), m_dt(1.0 / (tps > 0 ? tps : 240.0)) {}
    ~GameSim() override;

    bool resetToStart() override;
    int32_t tick() const override;
    void input(bool down, bool p2) override;
    bool step() override;
    bool dead() const override;
    bool finished() const override;
    SimPos pos() const override;
    SimContext context() const override;
    int snapshot() override;
    bool restore(int handle) override;
    void release(int handle) override;
    void syncHold(bool held1, bool held2) override;

private:
    bool holding(PlayerObject* p) const;
    PlayLayer* m_pl;
    double m_dt;
    std::vector<geode::Ref<CheckpointObject>> m_snaps;
};

}  // namespace afwc::hook
