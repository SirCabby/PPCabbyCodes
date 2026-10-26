// The library's entry. Unity preloads every library in
// PapersPlease_Data/Plugins at start-up ("Preloaded 'libPPCabbyCodes.so'" in
// Player.log), on the main thread; this one is a library of its own there, so
// no game file is replaced.
//
// Unity loads GameAssembly.so (the game's code) and starts the IL2CPP runtime
// in it before it preloads the plugins, so at load the runtime is normally up
// and none of the game's code has run yet: the mod finds the game's classes
// and hooks them there and then, on the main thread. Should the runtime not be
// started yet, the mod hooks il2cpp_init and does the same as it returns; and
// should GameAssembly.so not even be loaded yet, the mod puts itself in
// UnityPlayer.so's GOT slot for dlsym, which UnityPlayer.so looks every
// il2cpp_* function up with, and hands back its own il2cpp_init.
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"
#include "game.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"
#include "overlay.h"
#include "version.h"

namespace ppcc {
namespace {

using DlsymFn = void* (*)(void*, const char*);
using Il2cppInitFn = int (*)(const char*);

Il2cppInitFn g_real_il2cpp_init = nullptr;
void* g_game_assembly = nullptr;
bool g_started = false;

int hooked_il2cpp_init(const char* domain_name) {
  const int r = g_real_il2cpp_init(domain_name);
  log_set_main_thread();
  logf("il2cpp_init(\"%s\") returned %d - finding the game", domain_name ? domain_name : "", r);
  if (!il2cpp::bound() && !il2cpp::bind(g_game_assembly)) {
    logf("the IL2CPP API is incomplete - the mod stays out of the game");
    return r;
  }
  game::start();
  return r;
}

// UnityPlayer.so's dlsym. Everything but il2cpp_init goes straight to the real
// one as a tail call, so dlsym still sees UnityPlayer.so as its caller (which
// RTLD_NEXT depends on).
void* hooked_dlsym(void* handle, const char* name) {
  if (handle != RTLD_DEFAULT && handle != RTLD_NEXT && name && !g_real_il2cpp_init &&
      std::strcmp(name, "il2cpp_init") == 0) {
    void* real = dlsym(handle, name);
    if (real) {
      g_game_assembly = handle;
      g_real_il2cpp_init = reinterpret_cast<Il2cppInitFn>(real);
      logf("UnityPlayer asked for il2cpp_init: the mod starts with the runtime");
      return reinterpret_cast<void*>(&hooked_il2cpp_init);
    }
    return real;
  }
  __attribute__((musttail)) return dlsym(handle, name);
}

std::string game_dir() {
  char exe[PATH_MAX] = {};
  const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n <= 0) return "./";
  std::string path(exe, static_cast<size_t>(n));
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? "./" : path.substr(0, slash + 1);
}

std::string exe_name() {
  char exe[PATH_MAX] = {};
  const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n <= 0) return {};
  std::string path(exe, static_cast<size_t>(n));
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

__attribute__((constructor)) void on_load() {
  if (g_started) return;
  g_started = true;
  // Only inside the game: anything else that loads the library (a test) gets
  // nothing.
  if (exe_name() != "PapersPlease") return;
  // The test harness (testkit.h) keeps its runs' log and settings in its own
  // directory, away from the player's.
  const char* test_dir = std::getenv("PPCC_TEST_DIR");
  const std::string dir = test_dir && *test_dir ? std::string(test_dir) + "/" : game_dir();
  log_init(dir.c_str());
  logf("PPCabbyCodes %s loaded (pid %d) from the game in %s", PPCC_VERSION, static_cast<int>(getpid()), dir.c_str());
  config::load(dir.c_str());

  if (!config::get().disable_overlay) overlay::install(dir.c_str());
  else logf("overlay disabled (Disable = overlay)");

  hook::Module ga;
  if (hook::find_module("GameAssembly.so", &ga)) {
    g_game_assembly = dlopen(ga.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!g_game_assembly || !il2cpp::bind(g_game_assembly)) {
      logf("the IL2CPP API in %s is incomplete - the mod stays out of the game", ga.path.c_str());
      return;
    }
    if (il2cpp::il2cpp_get_corlib()) {
      log_set_main_thread();
      logf("the IL2CPP runtime is up - finding the game");
      game::start();
      return;
    }
    void* init = dlsym(g_game_assembly, "il2cpp_init");
    if (!hook::inline_hook(init, reinterpret_cast<void*>(&hooked_il2cpp_init),
                           reinterpret_cast<void**>(&g_real_il2cpp_init), "il2cpp_init"))
      logf("il2cpp_init could not be hooked - the mod stays out of the game");
    return;
  }
  void* previous = nullptr;
  if (!hook::got_hook("UnityPlayer.so", "dlsym", reinterpret_cast<void*>(&hooked_dlsym), &previous))
    logf("UnityPlayer.so's dlsym could not be hooked - the mod stays out of the game");
}

}  // namespace
}  // namespace ppcc
