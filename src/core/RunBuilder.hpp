#pragma once
#include "Types.hpp"
#include <algorithm>

namespace afwc {

inline void sortEvents(std::vector<InputEvent>& ev) {
    std::stable_sort(ev.begin(), ev.end(), [](InputEvent const& a, InputEvent const& b) { return a.tick < b.tick; });
}

// Pair every press with the next release of the same player and (re)build run.clicks.
// Existing scan results are NOT kept: call this only when the event list changed.
inline void deriveClicks(Run& run) {
    sortEvents(run.events);
    run.clicks.clear();
    int open[2] = {-1, -1};  // index into run.clicks of the currently open press per player
    for (auto const& e : run.events) {
        int p = e.p2 ? 1 : 0;
        if (e.down) {
            if (open[p] >= 0) continue;  // duplicate press while already held: ignore
            Click c;
            c.tick = e.tick;
            c.p2 = e.p2;
            open[p] = (int)run.clicks.size();
            run.clicks.push_back(c);
        } else if (open[p] >= 0) {
            run.clicks[open[p]].release = e.tick;
            open[p] = -1;
        }
    }
}

// Is the button of the given player held at the START of tick `tick` (i.e. after all events with e.tick < tick)?
inline bool heldBefore(std::vector<InputEvent> const& ev, int32_t tick, bool p2) {
    bool held = false;
    for (auto const& e : ev) {
        if (e.tick >= tick) break;
        if (e.p2 == p2) held = e.down;
    }
    return held;
}

}  // namespace afwc
