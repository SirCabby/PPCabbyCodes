#include "game.h"

#include <cstring>
#include <string>

#include "cheats.h"
#include "config.h"
#include "hook.h"
#include "log.h"
#include "overlay.h"
#include "panel.h"
#include "platform.h"
#include "switches.h"
#include "testkit.h"
#include "unity.h"

namespace ppcc::game {
namespace {

using il2cpp::Object;
using il2cpp::Method;

// InputWrapper.PointerState, returned by value (16 bytes: the SysV ABI hands
// it back in xmm0 and rax, Windows' x64 ABI through a pointer the caller passes
// first; the compiler does the same for this struct on each).
struct PointerState {
  float x, y, z;
  bool down_l, down_r, valid, from_mouse;
};
static_assert(sizeof(PointerState) == 16);

using UpdateFn = void (*)(Object*, const Method*);
using PointerFn = PointerState (*)(Object*, int32_t, const Method*);
using KeyStateFn = bool (*)(Object*, int32_t, const Method*);

UpdateFn g_update = nullptr;
PointerFn g_pointer = nullptr;
KeyStateFn g_key_state = nullptr;

int off_host_game = -1;    // HostUnity.game (IGame: the Game)
int off_game_pause = -1;   // Game.pauseScreen
Object* g_game = nullptr;
bool g_paused = false;
bool g_panel_hidden = false;
bool g_hooked = false;
std::string g_problem;
long g_frames = 0;
long g_start_tid = 0;
bool g_tick_failed = false;

void publish_input() {
  if (config::get().disable_input) return;
  overlay::InputSample s;
  s.screen_w = unity::screen_width();
  s.screen_h = unity::screen_height();
  const testkit::Pointer t = testkit::pointer();
  if (t.active) {
    s.x = t.x;
    s.y = t.y;
    s.buttons[0] = t.down;
  } else {
    const unity::Vector3 p = unity::mouse_position();
    s.x = p.x;
    s.y = static_cast<float>(s.screen_h) - p.y;  // Unity counts from the bottom
    for (int b = 0; b < 3; ++b) s.buttons[b] = unity::mouse_button(b) || unity::mouse_button_down(b);
    s.wheel = unity::mouse_scroll().y;
  }
  overlay::publish_input(s);
}

void tick(Object* host) {
  if (++g_frames == 1) {
    // The game's thread is the one its frames run on; the mod started on the
    // thread that loaded it.
    const long start_tid = g_start_tid, now = platform::thread_id();
    log_set_main_thread();
    if (now != start_tid) logf("game: frames run on thread %ld, the mod started on %ld", now, start_tid);
    else logf("game: first frame (thread %ld, as the mod's start)", now);
  }
  testkit::tick();
  g_game = il2cpp::get<Object*>(host, off_host_game);
  g_paused = g_game && il2cpp::get<Object*>(g_game, off_game_pause) != nullptr;
  publish_input();
  if (unity::key_down(config::get().toggle_key) && g_paused) g_panel_hidden = !g_panel_hidden;

  const unsigned requests = panel::take_requests();
  if (requests & panel::kRequestSave) {
    config::Settings& s = config::edit();
    s.lock_materials = switches::get(switches::kLockMaterials);
    s.cross_off_criminals = switches::get(switches::kCrossOffCriminals);
    s.pause_after_failure = switches::get(switches::kPauseAfterFailure);
  }
  if (!config::get().disable_features) cheats::tick(g_game, requests);
  if (requests & panel::kRequestSave) config::save();

  panel::Model m;
  m.paused = g_paused;
  m.show_menu = (g_paused || config::get().always_show || testkit::force_panel()) && !g_panel_hidden;
  m.ready = g_hooked;
  m.problem = g_problem;
  m.toggle_key = config::get().toggle_key_name;
  cheats::describe(m);
  panel::publish(m);
}

void hooked_update(Object* host, const Method* method) {
  if (!g_tick_failed && !platform::guarded([](void* h) { tick(static_cast<Object*>(h)); }, host)) {
    // A managed exception out of a game call the mod made: the mod's frame
    // stops for good, the game's goes on.
    g_tick_failed = true;
    logf("the mod's frame threw (frame %ld) - the mod stops here", g_frames);
  }
  g_update(host, method);
}

PointerState hooked_pointer(Object* self, int32_t pointer_id, const Method* method) {
  PointerState s = g_pointer(self, pointer_id, method);
  const testkit::Pointer t = testkit::pointer();
  if (t.active && pointer_id == 0) {
    s.x = t.x;
    s.y = static_cast<float>(unity::screen_height()) - t.y;
    s.z = 0;
    s.down_l = t.down;
    s.down_r = false;
    s.valid = true;
    s.from_mouse = true;
  }
  if (overlay::wants_mouse() && !config::get().disable_input) {
    s.down_l = false;
    s.down_r = false;
  }
  return s;
}

bool hooked_key_state(Object* self, int32_t key, const Method* method) {
  return testkit::key_held(key) || g_key_state(self, key, method);
}

}  // namespace

Object* current_game() { return g_game; }
bool paused() { return g_paused; }

void start() {
  g_start_tid = platform::thread_id();
  const config::Settings& cfg = config::get();
  switches::on[switches::kLockMaterials].store(cfg.lock_materials);
  switches::on[switches::kCrossOffCriminals].store(cfg.cross_off_criminals);
  switches::on[switches::kPauseAfterFailure].store(cfg.pause_after_failure);

  testkit::init();
  if (!unity::bind()) g_problem = "Unity's input calls are missing";
  const char* kAsm = "Assembly-CSharp.dll";
  il2cpp::Class* host = il2cpp::find_class(kAsm, "", "HostUnity");
  il2cpp::Class* input = il2cpp::find_class(kAsm, "", "InputWrapper");
  il2cpp::Class* game = il2cpp::find_class(kAsm, "", "Game");
  const Method* update = host ? il2cpp::find_method(host, "Update", 0) : nullptr;
  const Method* pointer = input ? il2cpp::find_method(input, "GetPointerState", 1) : nullptr;
  off_host_game = host ? il2cpp::field_offset(host, "game") : -1;
  off_game_pause = game ? il2cpp::field_offset(game, "pauseScreen") : -1;
  if (!update || off_host_game < 0 || off_game_pause < 0) {
    g_problem = "the game's main loop was not found (another version of the game?)";
    logf("game: %s", g_problem.c_str());
    return;
  }
  if (!hook::inline_hook(update->pointer, reinterpret_cast<void*>(&hooked_update),
                         reinterpret_cast<void**>(&g_update), "HostUnity.Update")) {
    g_problem = "HostUnity.Update could not be hooked";
    return;
  }
  if (pointer)
    hook::inline_hook(pointer->pointer, reinterpret_cast<void*>(&hooked_pointer), reinterpret_cast<void**>(&g_pointer),
                      "InputWrapper.GetPointerState");
  if (testkit::active()) {
    if (const Method* key = il2cpp::find_method(input, "GetKeyState", 1))
      hook::inline_hook(key->pointer, reinterpret_cast<void*>(&hooked_key_state),
                        reinterpret_cast<void**>(&g_key_state), "InputWrapper.GetKeyState");
  }
  g_hooked = true;
  if (!cfg.disable_features) cheats::start();
  logf("game: hooked - the panel opens with the pause menu");
}

}  // namespace ppcc::game
