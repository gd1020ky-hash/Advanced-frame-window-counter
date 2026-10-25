// AFWC - in-level overlay: label counters, click markers, info line.
#pragma once
#include <Geode/Geode.hpp>

namespace afwc::overlay {

void attach(PlayLayer* pl);                 // build the nodes (PlayLayer::init)
void detach();                              // drop every node / reference (level closed)
void frame(PlayLayer* pl, int32_t tick, float dt);   // once per rendered frame, after the physics steps
void rewound(PlayLayer* pl, int32_t tick);  // the level jumped back (death, checkpoint, restart)
void refresh();                             // run / presets / settings changed

}  // namespace afwc::overlay
