#pragma once

#include <string>

#include "il2cpp.h"

// Lock Inspector Materials: the rulebook ("Rules") and the bulletin
// ("Bulletin") stay where they were locked. Dragging them does not move them
// (DeskItem.updateCustomDrag, the per-frame move of a drag, is skipped for
// them; a click still reaches the page, so tabs and page corners work). Each
// day's booth puts them there, open at the locked pages (by page id, so a
// rulebook that has gained pages still opens at the same page).
namespace ppcc::lock {

bool start();  // hooks the drag; reads the saved lock from the ini

// Every frame, on the game's thread. `on`: the switch; switched on in a booth,
// it locks them where they are.
void tick(il2cpp::Object* booth, bool on);

std::string describe();  // for the panel

}  // namespace ppcc::lock
