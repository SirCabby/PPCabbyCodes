#include "config.h"

#include <strings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "log.h"
#include "platform.h"

namespace ppcc::config {
namespace {

// The ini's state, made on first use: config::load runs from the library's
// constructor, before other files' globals are initialised.
struct State {
  Settings settings;
  std::string path;
  std::map<std::string, std::string> values;  // every key=value of the file, as text
  std::mutex lock;
};
State& state() {
  static State* s = new State;
  return *s;
}

bool truthy(const std::string& v) {
  return !strcasecmp(v.c_str(), "1") || !strcasecmp(v.c_str(), "on") || !strcasecmp(v.c_str(), "true") ||
         !strcasecmp(v.c_str(), "yes");
}

std::string trim(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// The key a line sets: the text before its '=', or empty for a comment, a
// section or a blank line.
std::string line_key(const std::string& line) {
  const std::string t = trim(line);
  if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') return {};
  const size_t eq = t.find('=');
  if (eq == std::string::npos) return {};
  return trim(t.substr(0, eq));
}

const char* kDefaults =
    "# PPCabbyCodes - a cheat panel for Papers, Please, shown while the game is paused.\n"
    "#\n"
    "#   LockInspectorMaterials = 0   ; the rulebook and the bulletin stay where they were locked; each new\n"
    "#                                ; day puts them there, open at the locked pages\n"
    "#   CrossOffCriminals      = 0   ; the bulletin's wanted criminals who have already come through the\n"
    "#                                ; booth lose their photos\n"
    "#   PauseAfterFailure      = 0   ; after a mistake the papers stay and the clock stops until Continue\n"
    "#   ToggleKey              = F7  ; hides/shows the panel while the game is paused\n"
    "#   AlwaysShow             = 0   ; debug: the panel on every screen, not only while paused\n"
    "#   Disable                =     ; comma list of subsystems to turn off: overlay,input,features\n"
    "#\n"
    "# The Lock* lines below the switches are written by the panel: where the locked\n"
    "# rulebook and bulletin go, and the pages they open at.\n"
    "LockInspectorMaterials = 0\n"
    "CrossOffCriminals = 0\n"
    "PauseAfterFailure = 0\n"
    "ToggleKey = F7\n"
    "AlwaysShow = 0\n"
    "Disable =\n";

void apply_values() {
  auto get = [](const char* k) -> const std::string* {
    auto it = state().values.find(k);
    return it == state().values.end() ? nullptr : &it->second;
  };
  if (auto v = get("LockInspectorMaterials")) state().settings.lock_materials = truthy(*v);
  if (auto v = get("CrossOffCriminals")) state().settings.cross_off_criminals = truthy(*v);
  if (auto v = get("PauseAfterFailure")) state().settings.pause_after_failure = truthy(*v);
  if (auto v = get("ToggleKey")) {
    const int k = key_code(*v);
    if (k >= 0) {
      state().settings.toggle_key = k;
      state().settings.toggle_key_name = *v;
    } else {
      logf("config: ToggleKey \"%s\" is not a key name the mod knows - F7 it is", v->c_str());
    }
  }
  if (auto v = get("AlwaysShow")) state().settings.always_show = truthy(*v);
  if (auto v = get("Disable")) {
    const std::string& d = *v;
    state().settings.disable_overlay = d.find("overlay") != std::string::npos;
    state().settings.disable_input = d.find("input") != std::string::npos;
    state().settings.disable_features = d.find("features") != std::string::npos;
  }
}

void sync_switches_to_values() {
  state().values["LockInspectorMaterials"] = state().settings.lock_materials ? "1" : "0";
  state().values["CrossOffCriminals"] = state().settings.cross_off_criminals ? "1" : "0";
  state().values["PauseAfterFailure"] = state().settings.pause_after_failure ? "1" : "0";
}

}  // namespace

const Settings& get() { return state().settings; }
Settings& edit() { return state().settings; }

int key_code(const std::string& raw) {
  const std::string name = trim(raw);
  if (name.empty()) return -1;
  char* end = nullptr;
  const long n = std::strtol(name.c_str(), &end, 0);
  if (end && *end == '\0') return static_cast<int>(n);
  if ((name[0] == 'F' || name[0] == 'f') && name.size() <= 3) {
    const int f = std::atoi(name.c_str() + 1);
    if (f >= 1 && f <= 15) return 281 + f;  // KeyCode.F1 = 282
  }
  struct Named {
    const char* name;
    int code;
  };
  static const Named kNamed[] = {{"Insert", 277}, {"Home", 278},     {"End", 279},      {"PageUp", 280},
                                 {"PageDown", 281}, {"Pause", 19},   {"ScrollLock", 302}, {"Backquote", 96}};
  for (const Named& k : kNamed)
    if (!strcasecmp(k.name, name.c_str())) return k.code;
  return -1;
}

void load(const char* dir) {
  std::lock_guard<std::mutex> guard(state().lock);
  state().path = std::string(dir) + "PPCabbyCodes.ini";
  FILE* f = platform::open_file(state().path, "r");
  if (!f) {
    if (FILE* w = platform::open_file(state().path, "w")) {
      std::fputs(kDefaults, w);
      std::fclose(w);
      logf("config: wrote the defaults to %s", state().path.c_str());
    }
    f = platform::open_file(state().path, "r");
  }
  if (!f) {
    logf("config: cannot read %s - using the defaults", state().path.c_str());
    return;
  }
  char line[1024];
  while (std::fgets(line, sizeof(line), f)) {
    const std::string l = line;
    const std::string k = line_key(l);
    if (k.empty()) continue;
    state().values[k] = trim(l.substr(l.find('=') + 1));
  }
  std::fclose(f);
  apply_values();
  logf("config: %s - lock %d, cross-off %d, pause after failure %d, toggle key %d%s", state().path.c_str(),
       state().settings.lock_materials, state().settings.cross_off_criminals, state().settings.pause_after_failure,
       state().settings.toggle_key, state().settings.always_show ? ", always show" : "");
}

std::string value(const char* key, const char* fallback) {
  std::lock_guard<std::mutex> guard(state().lock);
  auto it = state().values.find(key);
  return it == state().values.end() ? std::string(fallback) : it->second;
}

void set_value(const char* key, const std::string& v) {
  std::lock_guard<std::mutex> guard(state().lock);
  state().values[key] = v;
}

bool save() {
  std::lock_guard<std::mutex> guard(state().lock);
  if (state().path.empty()) return false;
  sync_switches_to_values();
  std::vector<std::string> lines;
  if (FILE* f = platform::open_file(state().path, "r")) {
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) lines.emplace_back(line);
    std::fclose(f);
  }
  if (lines.empty()) {
    for (const char* p = kDefaults; *p;) {
      const char* nl = std::strchr(p, '\n');
      const size_t n = nl ? static_cast<size_t>(nl - p + 1) : std::strlen(p);
      lines.emplace_back(p, n);
      p += n;
    }
  }
  std::map<std::string, bool> written;
  for (std::string& l : lines) {
    const std::string k = line_key(l);
    if (k.empty()) continue;
    auto it = state().values.find(k);
    if (it == state().values.end()) continue;
    l = k + " = " + it->second + "\n";
    written[k] = true;
  }
  for (const auto& [k, v] : state().values)
    if (!written.count(k)) lines.push_back(k + " = " + v + "\n");
  const std::string tmp = state().path + ".new";
  FILE* w = platform::open_file(tmp, "w");
  if (!w) return false;
  for (const std::string& l : lines) std::fputs(l.c_str(), w);
  const bool ok = std::fclose(w) == 0 && platform::replace_file(tmp, state().path);
  if (!ok) logf("config: could not write %s", state().path.c_str());
  return ok;
}

}  // namespace ppcc::config
