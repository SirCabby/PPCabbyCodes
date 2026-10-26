#pragma once

#include "il2cpp.h"
#include "panel.h"

// The three switches' work: finding the game's classes, hooking the methods
// they change, and their per-frame upkeep.
namespace ppcc::cheats {

void start();                                       // once, on the game's thread, at il2cpp_init
void tick(il2cpp::Object* game, unsigned requests);  // every frame, before the game's own update
void describe(panel::Model& m);                     // what the panel shows

// The test harness (testkit.h): the game's state in the log, and a panel
// request as if the panel had posted it.
void log_state();
void inject_request(unsigned request);

}  // namespace ppcc::cheats
