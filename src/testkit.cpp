#include "testkit.h"

#include <strings.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>

#include "cheats.h"
#include "game.h"
#include "hook.h"
#include "il2cpp.h"
#include "pp.h"
#include "log.h"
#include "panel.h"
#include "platform.h"
#include "switches.h"

namespace ppcc::testkit {
namespace {

std::string g_dir;
bool g_active = false;
long g_seq = 0;

struct Step {
  enum Kind { kMove, kPress, kRelease, kKeyDown, kKeyUp, kWait, kShot, kState, kSwitch, kRequest, kPanel, kAck,
              kUntilScreen, kUntilBooth, kSleep, kCriminalUsed, kClickContinue, kClock, kTravelers, kThrow };
  Kind kind;
  float x = 0, y = 0;
  int n = 0;
  std::string text;
  std::chrono::steady_clock::time_point deadline{};  // an "until" step gives up then
};
std::deque<Step> g_queue;
int g_wait = 0;
std::chrono::steady_clock::time_point g_sleep_until;

// The pointer, keys and shot are read from other threads: made on first use,
// never destroyed.
std::mutex& state_lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::string& shot_path() {
  static std::string* s = new std::string;
  return *s;
}
Pointer g_pointer;
int g_key = -1;
std::atomic<bool> g_force_panel{false};

int game_key(const std::string& name) {
  static il2cpp::Class* key_class = nullptr;
  if (!key_class) key_class = il2cpp::find_class("Assembly-CSharp.dll", "app.ent", "Key");
  if (!key_class) return -1;
  il2cpp::il2cpp_runtime_class_init(key_class);
  il2cpp::Field* f = il2cpp::il2cpp_class_get_field_from_name(key_class, name.c_str());
  if (!f) return -1;
  int v = -1;
  il2cpp::il2cpp_field_static_get_value(f, &v);
  return v;
}

void parse(const std::string& line) {
  std::istringstream in(line);
  std::string cmd;
  in >> cmd;
  auto step = [](Step::Kind k) {
    Step s;
    s.kind = k;
    return s;
  };
  auto hold = [&](int frames) {
    Step w = step(Step::kWait);
    w.n = frames;
    g_queue.push_back(w);
  };
  if (cmd == "move") {
    Step s = step(Step::kMove);
    in >> s.x >> s.y;
    g_queue.push_back(s);
  } else if (cmd == "press") {
    g_queue.push_back(step(Step::kPress));
  } else if (cmd == "release") {
    g_queue.push_back(step(Step::kRelease));
  } else if (cmd == "click") {
    Step s = step(Step::kMove);
    in >> s.x >> s.y;
    g_queue.push_back(s);
    hold(3);
    g_queue.push_back(step(Step::kPress));
    hold(4);
    g_queue.push_back(step(Step::kRelease));
    hold(3);
  } else if (cmd == "drag") {
    float x1, y1, x2, y2;
    int n = 20;
    in >> x1 >> y1 >> x2 >> y2;
    if (!(in >> n)) n = 20;
    if (n < 1) n = 1;
    Step s = step(Step::kMove);
    s.x = x1;
    s.y = y1;
    g_queue.push_back(s);
    hold(3);
    g_queue.push_back(step(Step::kPress));
    hold(4);
    for (int i = 1; i <= n; ++i) {
      Step m = step(Step::kMove);
      m.x = x1 + (x2 - x1) * static_cast<float>(i) / static_cast<float>(n);
      m.y = y1 + (y2 - y1) * static_cast<float>(i) / static_cast<float>(n);
      g_queue.push_back(m);
      hold(1);
    }
    hold(3);
    g_queue.push_back(step(Step::kRelease));
    hold(3);
  } else if (cmd == "key") {
    std::string name;
    in >> name;
    Step s = step(Step::kKeyDown);
    s.n = game_key(name);
    if (s.n < 0) {
      logf("testkit: no game key %s", name.c_str());
      return;
    }
    g_queue.push_back(s);
    hold(4);
    g_queue.push_back(step(Step::kKeyUp));
    hold(3);
  } else if (cmd == "ms") {
    Step s = step(Step::kSleep);
    in >> s.n;
    g_queue.push_back(s);
  } else if (cmd == "wait") {
    int n = 1;
    in >> n;
    hold(n);
  } else if (cmd == "shot") {
    Step s = step(Step::kShot);
    in >> s.text;
    g_queue.push_back(s);
    hold(3);
  } else if (cmd == "state") {
    g_queue.push_back(step(Step::kState));
  } else if (cmd == "switch") {
    Step s = step(Step::kSwitch);
    in >> s.text >> s.n;
    g_queue.push_back(s);
  } else if (cmd == "request") {
    Step s = step(Step::kRequest);
    in >> s.text;
    g_queue.push_back(s);
  } else if (cmd == "screen" || cmd == "booth") {
    // screen NAME [frames]: until the game's screen is that class (TitleScreen,
    // LoadScreen, NewsScreen, DayScreen...); booth STATE [frames]: until the
    // booth's state machine is in that state (empty, working, waiting-to-leave...).
    Step s = step(cmd == "screen" ? Step::kUntilScreen : Step::kUntilBooth);
    in >> s.text;
    if (!(in >> s.n)) s.n = 60000;  // milliseconds
    g_queue.push_back(s);
  } else if (cmd == "travelers") {
    // travelers N: the day counts N travelers processed (a day ends only after its minimum)
    Step s = step(Step::kTravelers);
    in >> s.n;
    g_queue.push_back(s);
  } else if (cmd == "clock") {
    // clock S: the booth's clock jumps S seconds ahead (to reach the day's end)
    Step s = step(Step::kClock);
    in >> s.x;
    g_queue.push_back(s);
  } else if (cmd == "continue") {
    // continue: click the hold's Continue button, wherever the overlay drew it
    g_queue.push_back(step(Step::kClickContinue));
  } else if (cmd == "crimused") {
    // crimused N: today's criminal N counts as having been at the booth
    Step s = step(Step::kCriminalUsed);
    in >> s.n;
    g_queue.push_back(s);
  } else if (cmd == "throw") {
    g_queue.push_back(step(Step::kThrow));
  } else if (cmd == "panel") {
    Step s = step(Step::kPanel);
    in >> s.n;
    g_queue.push_back(s);
  } else if (!cmd.empty() && cmd[0] != '#') {
    logf("testkit: unknown command %s", line.c_str());
  }
}

void read_commands() {
  const std::string path = g_dir + "/cmd";
  const std::string taking = g_dir + "/cmd.taking";
  if (!platform::replace_file(path, taking)) return;
  FILE* f = platform::open_file(taking, "r");
  if (!f) return;
  char line[512];
  long seq = -1;
  while (std::fgets(line, sizeof(line), f)) {
    std::string l(line);
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    // "#seq N" (tools/headless.sh): the batch's number, answered in ack.
    if (l.rfind("#seq ", 0) == 0) {
      seq = std::atol(l.c_str() + 5);
      continue;
    }
    logf("testkit: %s", l.c_str());
    parse(l);
  }
  std::fclose(f);
  platform::remove_file(taking);
  Step ack;
  ack.kind = Step::kAck;
  ack.n = static_cast<int>(seq);
  g_queue.push_back(ack);
}

void run(const Step& s) {
  switch (s.kind) {
    case Step::kMove: {
      std::lock_guard<std::mutex> guard(state_lock());
      g_pointer.active = true;
      g_pointer.x = s.x;
      g_pointer.y = s.y;
      break;
    }
    case Step::kPress:
    case Step::kRelease: {
      std::lock_guard<std::mutex> guard(state_lock());
      g_pointer.active = true;
      g_pointer.down = s.kind == Step::kPress;
      break;
    }
    case Step::kKeyDown: {
      std::lock_guard<std::mutex> guard(state_lock());
      g_key = s.n;
      break;
    }
    case Step::kKeyUp: {
      std::lock_guard<std::mutex> guard(state_lock());
      g_key = -1;
      break;
    }
    case Step::kWait:
      g_wait = s.n;
      break;
    case Step::kSleep:
      g_sleep_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(s.n);
      break;
    case Step::kShot: {
      std::lock_guard<std::mutex> guard(state_lock());
      shot_path() = g_dir + "/" + s.text + ".ppm";
      break;
    }
    case Step::kState:
      cheats::log_state();
      break;
    case Step::kSwitch: {
      int k = -1;
      if (!strcasecmp(s.text.c_str(), "lock")) k = switches::kLockMaterials;
      if (!strcasecmp(s.text.c_str(), "crossoff")) k = switches::kCrossOffCriminals;
      if (!strcasecmp(s.text.c_str(), "pause")) k = switches::kPauseAfterFailure;
      if (k >= 0) switches::on[k].store(s.n != 0);  // the mod's next frame takes it up
      break;
    }
    case Step::kRequest:
      if (s.text == "continue") cheats::inject_request(panel::kRequestContinue);
      break;
    case Step::kPanel:
      g_force_panel.store(s.n != 0);
      break;
    case Step::kUntilScreen:
    case Step::kUntilBooth:
      break;  // handled by waiting_until
    case Step::kClock: {
      il2cpp::Object* booth = pp::booth_of(game::current_game());
      il2cpp::Object* clock = il2cpp::get<il2cpp::Object*>(booth, pp::F.booth_clock);
      if (clock) {
        il2cpp::set<double>(clock, pp::F.clock_start, il2cpp::get<double>(clock, pp::F.clock_start) - s.x);
        logf("testkit: booth clock %+.0f s", static_cast<double>(s.x));
      }
      break;
    }
    case Step::kTravelers: {
      il2cpp::Object* booth = pp::booth_of(game::current_game());
      il2cpp::Object* engine = il2cpp::get<il2cpp::Object*>(booth, pp::F.booth_engine);
      static int off = -1;
      if (off < 0 && engine) off = il2cpp::field_offset(engine->klass, "numTravelers");
      il2cpp::set<int32_t>(engine, off, s.n);
      logf("testkit: %d travelers processed", s.n);
      break;
    }
    case Step::kClickContinue: {
      float x = 0, y = 0;
      if (!panel::continue_button_center(&x, &y)) {
        logf("testkit: no Continue button on screen");
        break;
      }
      // the same as "click x y"
      Step m;
      m.kind = Step::kMove;
      m.x = x;
      m.y = y;
      Step w;
      w.kind = Step::kWait;
      w.n = 3;
      Step p;
      p.kind = Step::kPress;
      Step w2 = w;
      w2.n = 4;
      Step r;
      r.kind = Step::kRelease;
      g_queue.push_front(w);
      g_queue.push_front(r);
      g_queue.push_front(w2);
      g_queue.push_front(p);
      g_queue.push_front(w);
      g_queue.push_front(m);
      break;
    }
    case Step::kCriminalUsed: {
      il2cpp::Object* booth = pp::booth_of(game::current_game());
      il2cpp::Object* day = il2cpp::get<il2cpp::Object*>(booth, pp::F.booth_day);
      il2cpp::Object* c = pp::array_get(il2cpp::get<il2cpp::Object*>(day, pp::F.day_criminals), s.n);
      il2cpp::set<bool>(c, pp::F.crim_used, true);
      logf("testkit: criminal %d marked used (%p)", s.n, static_cast<void*>(c));
      break;
    }
    case Step::kThrow: {
      // Nothing of the mod's frame runs after this: the batch is answered first.
      while (!g_queue.empty() && g_queue.front().kind == Step::kAck) {
        const Step ack = g_queue.front();
        g_queue.pop_front();
        run(ack);
      }
      auto argument_null = reinterpret_cast<il2cpp::Object* (*)(const char*)>(
          platform::symbol(il2cpp::library(), "il2cpp_get_exception_argument_null"));
      auto raise = reinterpret_cast<void (*)(il2cpp::Object*)>(
          platform::symbol(il2cpp::library(), "il2cpp_raise_exception"));
      if (!argument_null || !raise) {
        logf("testkit: no il2cpp_raise_exception to throw with");
        break;
      }
      logf("testkit: the game's code raises a managed exception in the mod's frame");
      raise(argument_null("testkit"));
      break;
    }
    case Step::kAck: {
      g_seq = s.n >= 0 ? s.n : g_seq + 1;
      // Binary: the harness compares the line as it is, and Windows' text mode
      // would end it with a CR.
      const std::string path = g_dir + "/ack";
      if (FILE* f = platform::open_file(path, "wb")) {
        std::fprintf(f, "%ld\n", g_seq);
        std::fclose(f);
      }
      break;
    }
  }
}

// A harness run stays away from Steam: no relaunch through Steam (which the
// game asks for when Steam did not start it - and which would put the game on
// the desktop), and no Steam session (the player's Steam status stays as it
// is). The game carries on without Steam as it does when SteamAPI_Init fails.
bool (*g_restart)(uint32_t, const il2cpp::Method*) = nullptr;
bool (*g_steam_init)(const il2cpp::Method*) = nullptr;
bool no_restart(uint32_t, const il2cpp::Method*) { return false; }
bool no_steam_init(const il2cpp::Method*) { return false; }

void keep_away_from_steam() {
  il2cpp::Class* api = il2cpp::find_class("com.rlabrecque.steamworks.net.dll", "Steamworks", "SteamAPI");
  const il2cpp::Method* restart = api ? il2cpp::find_method(api, "RestartAppIfNecessary", 1) : nullptr;
  const il2cpp::Method* init = api ? il2cpp::find_method(api, "Init", 0) : nullptr;
  if (restart)
    hook::inline_hook(restart->pointer, reinterpret_cast<void*>(&no_restart), reinterpret_cast<void**>(&g_restart),
                      "SteamAPI.RestartAppIfNecessary (testkit)");
  if (init)
    hook::inline_hook(init->pointer, reinterpret_cast<void*>(&no_steam_init), reinterpret_cast<void**>(&g_steam_init),
                      "SteamAPI.Init (testkit)");
}

// True while an "until" step at the head of the queue still waits.
bool waiting_until() {
  if (g_queue.empty()) return false;
  Step& s = g_queue.front();
  if (s.kind != Step::kUntilScreen && s.kind != Step::kUntilBooth) return false;
  bool met = false;
  il2cpp::Object* game = game::current_game();
  if (s.kind == Step::kUntilScreen) {
    il2cpp::Object* screen = il2cpp::get<il2cpp::Object*>(game, pp::F.game_screen);
    met = screen && s.text == il2cpp::class_name(screen);
  } else {
    il2cpp::Object* booth = pp::booth_of(game);
    met = booth && pp::str(pp::state_name(il2cpp::get<il2cpp::Object*>(booth, pp::F.booth_stater))) == s.text;
  }
  if (met) {
    g_queue.pop_front();
    return false;
  }
  const auto now = std::chrono::steady_clock::now();
  if (s.deadline == std::chrono::steady_clock::time_point{}) s.deadline = now + std::chrono::milliseconds(s.n);
  if (now >= s.deadline) {
    logf("testkit: gave up waiting for %s %s", s.kind == Step::kUntilScreen ? "screen" : "booth state",
         s.text.c_str());
    g_queue.pop_front();
    return false;
  }
  return true;
}


}  // namespace

bool active() { return g_active; }

void init() {
  const char* dir = std::getenv("PPCC_TEST_DIR");
  if (!dir || !*dir) return;
  g_dir = dir;
  g_active = true;
  logf("testkit: on, driven through %s", g_dir.c_str());
  keep_away_from_steam();
}


void tick() {
  if (!g_active) return;
  // $PPCC_TEST_DIR/abort drops whatever is queued (a script that went wrong).
  if (platform::remove_file(g_dir + "/abort")) {
    g_queue.clear();
    g_wait = 0;
    g_sleep_until = {};
    logf("testkit: aborted");
    Step ack;
    ack.kind = Step::kAck;
    g_queue.push_back(ack);
  }
  if (g_queue.empty()) read_commands();
  if (g_wait > 0) {
    --g_wait;
    return;
  }
  if (std::chrono::steady_clock::now() < g_sleep_until) return;
  // Run steps until one asks to wait.
  while (!g_queue.empty() && g_wait == 0 && std::chrono::steady_clock::now() >= g_sleep_until) {
    if (waiting_until()) return;
    if (g_queue.empty()) break;
    const Step s = g_queue.front();
    g_queue.pop_front();
    run(s);
  }
}

Pointer pointer() {
  std::lock_guard<std::mutex> guard(state_lock());
  return g_pointer;
}

bool key_held(int game_key) {
  std::lock_guard<std::mutex> guard(state_lock());
  return g_active && g_key >= 0 && g_key == game_key;
}

std::string take_shot_request() {
  std::lock_guard<std::mutex> guard(state_lock());
  std::string s;
  s.swap(shot_path());
  return s;
}

bool force_panel() { return g_force_panel.load(); }

}  // namespace ppcc::testkit
