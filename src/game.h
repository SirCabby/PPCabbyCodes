#pragma once

#include "il2cpp.h"

// The mod's hold on the game: HostUnity.Update (Unity's per-frame call into the
// game, on its main thread) runs the mod's frame first, and
// InputWrapper.GetPointerState (where the game reads the mouse) keeps the
// panel's clicks from the game.
namespace ppcc::game {

// Called once on the game's thread, as il2cpp_init returns: before any of the
// game's code has run, so the hooks go in with nothing executing them.
void start();

// The game's objects as of this frame (null outside them).
il2cpp::Object* current_game();

// True while the game's pause menu is up.
bool paused();

}  // namespace ppcc::game
