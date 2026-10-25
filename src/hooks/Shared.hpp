#pragma once
#include <cstdint>

namespace afwc::hook {

// Flags shared between the hooks and the scan driver.
struct Flags {
    bool scanActive = false;   // a scan owns the game: real updates are blocked, deaths/completions are intercepted
    bool stepping = false;     // we are inside GameSim::step (the update hook must let the call through)
    bool injecting = false;    // we are inside GameSim::input / syncHold (the recorder must ignore the press)
    bool dead = false;         // PlayLayer::destroyPlayer was reached during a scan
    bool finished = false;     // PlayLayer::levelComplete was reached during a scan
};
Flags& flags();

// Tick bookkeeping for one GJBaseGameLayer::update call. The tick of an input that the engine processes inside
// step k of an update is  (counter at update entry) + k  -- exactly the value GameSim::tick() shows before the
// same step, whatever place inside the step the engine increments its own counter.
struct StepState {
    bool inUpdate = false;
    int32_t base = 0;
    int steps = 0;
};
StepState& stepState();

}  // namespace afwc::hook
