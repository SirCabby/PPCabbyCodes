#pragma once

#include <string>

// PPCabbyCodes.ini in the game folder: written with documented defaults the
// first time, so the options are discoverable. The panel writes its switches
// (and what the material lock remembers) back as they change.
namespace ppcc::config {

struct Settings {
  // The switches, as the game starts: what the panel last left them at.
  bool lock_materials = false;
  bool cross_off_criminals = false;
  bool pause_after_failure = false;
  int toggle_key = 288;  // Unity KeyCode.F7: hides/shows the panel while the game is paused
  std::string toggle_key_name = "F7";
  bool always_show = false;  // debug: the panel on every screen, not only while paused
  // "Disable = overlay,input,features" turns whole subsystems off so a fault
  // can be bisected to one of them.
  bool disable_overlay = false;
  bool disable_input = false;
  bool disable_features = false;
};

const Settings& get();
Settings& edit();  // the panel's switches; save() writes them

// Loads <dir>PPCabbyCodes.ini (writing the defaults first if it is missing).
void load(const char* dir);

// The ini's raw value for a key the panel stores (the lock's positions), or
// `fallback`.
std::string value(const char* key, const char* fallback = "");
void set_value(const char* key, const std::string& v);

// Writes the switches and stored values back. Only their lines change - every
// other line, its comments and the file's order are kept; new keys go at the
// end - and the file is replaced whole (written beside it, then renamed).
bool save();

// Unity KeyCode for a key name in the ini ("F7", "Insert", a number), or -1.
int key_code(const std::string& name);

}  // namespace ppcc::config
