#pragma once

#include <string>

// The cheat panel: what is drawn, and how its buttons reach the game. The
// game's thread owns the game; the render thread owns ImGui. The game's
// thread publishes a Model every frame; the panel draws from the latest one
// and posts its requests back as flags the game's thread takes.
namespace ppcc::panel {

struct Model {
  bool paused = false;          // the game's pause menu is up
  bool show_menu = false;       // paused (or AlwaysShow) and not hidden with the toggle key
  bool holding = false;         // pause-after-failure is holding the booth after a mistake
  bool ready = false;           // the game's classes were found and the hooks are in
  std::string problem;          // why not, or which switch is unavailable
  std::string lock_text;        // what the material lock holds
  std::string criminals_text;   // the wanted list today
  std::string hold_text;        // what the held mistake was
  bool feature_ok[3] = {false, false, false};
  std::string toggle_key = "F7";  // the ini's ToggleKey, as written there
};

void publish(const Model& m);

// Requests from the panel, taken (and cleared) by the game's thread.
enum Request : unsigned {
  kRequestContinue = 1u << 0,    // let the held mistake go on as the game meant
  kRequestSave = 1u << 1,        // a switch changed: write the ini
};
unsigned take_requests();

// Draws the panel's windows; called by the overlay inside an ImGui frame.
void draw();

// Where the Continue button was last drawn (window pixels), for the test
// harness. False when it is not on screen.
bool continue_button_center(float* x, float* y);

}  // namespace ppcc::panel
