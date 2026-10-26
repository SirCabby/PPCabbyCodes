// The DLL's entry on Windows. UnityPlayer.dll imports HID.DLL and the mod is
// that DLL (win/proxy.h), so Windows loads it with the game and calls its
// DllMain as the process starts - under the loader lock, where only the least
// is safe, and before GameAssembly.dll (the game's code) is even loaded.
//
// So DllMain forwards hid.dll's functions, starts the log and the ini, and puts
// the mod in UnityPlayer.dll's import slot for GetProcAddress, which UnityPlayer
// looks up every il2cpp_* function in GameAssembly.dll with, as it loads it
// later, and dxgi.dll's CreateDXGIFactory2 (win/overlay_d3d11.cpp). For
// il2cpp_init it hands back the mod's own, which starts the mod as the runtime
// comes up: on the main thread, out of the loader lock, before any of the
// game's code has run - as linux/main.cpp's fallback does with dlsym.
//
// The game's mod loaders want the same slot. MelonLoader (version.dll) and
// BepInEx's Doorstop (winhttp.dll) each put their GetProcAddress there as their
// DllMain runs, and theirs does not call on to what the slot held: whoever is
// last in it is the only one asked. So the mod calls on to what it found there,
// and takes the slot again as GameAssembly.dll loads - after every DllMain of the
// game's start, before UnityPlayer looks anything up in it - so that it is in
// front of a loader's, whichever of the DllMains ran first. The loader still
// answers every name: the mod only wraps what it hands back.
#include <windows.h>

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
#include "win/overlay_d3d11.h"
#include "win/proxy.h"

namespace ppcc {
namespace {

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using Il2cppInitFn = int (*)(const char*);

// What UnityPlayer.dll's GetProcAddress slot held before the mod: Windows' own,
// or a loader's hook, which the mod calls on to (hook::slot_in_front sets it,
// before the mod's hook is in the slot).
void* g_next = reinterpret_cast<void*>(&GetProcAddress);
void** g_slot = nullptr;
thread_local int t_asking = 0;  // the mod's GetProcAddress is under way on this thread
Il2cppInitFn g_real_il2cpp_init = nullptr;
HMODULE g_game_assembly = nullptr;
bool g_overlay = false;

std::string utf8(const wchar_t* w, int n = -1) {
  const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
  if (len <= 0) return {};
  std::string s(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), len, nullptr, nullptr);
  if (n < 0) s.pop_back();  // the terminator, counted with -1
  return s;
}

std::wstring exe_path() {
  wchar_t path[MAX_PATH * 4] = {};
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 4);
  return n ? std::wstring(path, n) : std::wstring();
}

// The file name of the module some code is in ("version.dll"), for the log.
std::string module_name_of(const void* p) {
  HMODULE m = nullptr;
  wchar_t path[MAX_PATH] = {};
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCWSTR>(p), &m) ||
      !GetModuleFileNameW(m, path, MAX_PATH))
    return "an unknown module";
  const wchar_t* file = wcsrchr(path, L'\\');
  return utf8(file ? file + 1 : path);
}

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

// UnityPlayer.dll's GetProcAddress (called from any of its threads). The name
// is looked up by what the slot held - a loader's hook may answer il2cpp_init
// with its own - and the mod wraps il2cpp_init and DXGI's factory functions,
// whatever answered them.
FARPROC WINAPI hooked_get_proc_address(HMODULE module, LPCSTR name) {
  // A hooker behind the mod that calls on to what it found in the slot - the
  // mod, before the mod took the slot again - comes back here: Windows' own
  // answers it, or the two would ask each other forever.
  if (t_asking) return GetProcAddress(module, name);
  ++t_asking;
  const FARPROC real = reinterpret_cast<GetProcAddressFn>(__atomic_load_n(&g_next, __ATOMIC_ACQUIRE))(module, name);
  --t_asking;
  if (!real || IS_INTRESOURCE(name)) return real;
  if (!g_real_il2cpp_init && std::strcmp(name, "il2cpp_init") == 0) {
    g_game_assembly = module;
    g_real_il2cpp_init = reinterpret_cast<Il2cppInitFn>(real);
    const std::string answered = module_name_of(reinterpret_cast<void*>(real));
    if (_stricmp(answered.c_str(), "GameAssembly.dll") == 0)
      logf("UnityPlayer asked for il2cpp_init: the mod starts with the runtime");
    else
      logf("UnityPlayer asked for il2cpp_init, which %s answered: the mod starts with the runtime, after it",
           answered.c_str());
    return reinterpret_cast<FARPROC>(&hooked_il2cpp_init);
  }
  if (g_overlay) return reinterpret_cast<FARPROC>(overlay::dxgi_export(name, reinterpret_cast<void*>(real)));
  return real;
}

// Puts the mod's GetProcAddress in front of whatever holds the slot.
void take_slot(const char* when) {
  if (!hook::slot_in_front(g_slot, reinterpret_cast<void*>(&hooked_get_proc_address), &g_next)) {
    logf("UnityPlayer.dll's GetProcAddress: the mod is still in front %s", when);
    return;
  }
  const std::string owner = module_name_of(__atomic_load_n(&g_next, __ATOMIC_ACQUIRE));
  const bool windows = _stricmp(owner.c_str(), "KERNEL32.dll") == 0 || _stricmp(owner.c_str(), "KERNELBASE.dll") == 0;
  logf("UnityPlayer.dll's GetProcAddress hooked %s%s%s", when, windows ? "" : ", in front of a hook of ",
       windows ? "" : (owner + "'s (a mod loader's: it still answers every name)").c_str());
}

// The loader's notice of a DLL loaded (ntdll's LdrRegisterDllNotification,
// which the SDK does not declare).
struct LdrString {
  USHORT length, maximum_length;
  PWSTR buffer;
};
struct LdrDllLoaded {
  ULONG flags;
  const LdrString* full_name;
  const LdrString* base_name;
  void* base;
  ULONG size;
};
using LdrNotifyFn = VOID(CALLBACK*)(ULONG reason, const LdrDllLoaded* data, void* context);
using LdrRegisterFn = LONG(NTAPI*)(ULONG flags, LdrNotifyFn fn, void* context, void** cookie);
constexpr ULONG kLdrDllLoaded = 1;

VOID CALLBACK on_dll_loaded(ULONG reason, const LdrDllLoaded* data, void*) {
  if (reason != kLdrDllLoaded || !data || !data->base_name || !data->base_name->buffer) return;
  static const wchar_t kGameAssembly[] = L"GameAssembly.dll";
  const size_t n = data->base_name->length / sizeof(wchar_t);
  if (n == wcslen(kGameAssembly) && _wcsnicmp(data->base_name->buffer, kGameAssembly, n) == 0)
    take_slot("as GameAssembly.dll loads");
}

// The test harness's stand-in for a mod loader whose DllMain runs after the
// mod's (PPCC_TEST_LOADER, with PPCC_TEST_DIR): it takes the GetProcAddress slot
// as MelonLoader and Doorstop do - il2cpp_init answered with its own, the rest
// asked of Windows' own - so the mod must take the slot back as GameAssembly.dll
// loads, and still start.
Il2cppInitFn g_stand_in_init = nullptr;
int stand_in_il2cpp_init(const char* domain_name) {
  logf("testkit: the stand-in loader's il2cpp_init runs");
  return g_stand_in_init(domain_name);
}
FARPROC WINAPI stand_in_get_proc_address(HMODULE module, LPCSTR name) {
  const FARPROC real = GetProcAddress(module, name);
  if (!real || IS_INTRESOURCE(name) || std::strcmp(name, "il2cpp_init") != 0) return real;
  g_stand_in_init = reinterpret_cast<Il2cppInitFn>(real);
  return reinterpret_cast<FARPROC>(&stand_in_il2cpp_init);
}

void on_load() {
  // Only inside the game: anything else that loads the mod gets only the
  // forwarding.
  const std::wstring exe = exe_path();
  const size_t slash = exe.find_last_of(L"\\/");
  const std::wstring file = slash == std::wstring::npos ? exe : exe.substr(slash + 1);
  if (lstrcmpiW(file.c_str(), L"PapersPlease.exe") != 0) return;
  // The test harness (testkit.h) keeps its runs' log and settings in its own
  // directory, away from the player's.
  const wchar_t* test_dir = _wgetenv(L"PPCC_TEST_DIR");
  const std::string dir = test_dir && *test_dir ? utf8(test_dir) + "\\"
                                                : utf8(exe.substr(0, slash == std::wstring::npos ? 0 : slash + 1).c_str());
  log_init(dir.c_str());
  logf("PPCabbyCodes %s loaded (pid %lu) from the game in %s", PPCC_VERSION, GetCurrentProcessId(), dir.c_str());
  if (!proxy::loaded()) logf("hid.dll: Windows' own could not be loaded - its functions fail");
  config::load(dir.c_str());

  if (!config::get().disable_overlay) g_overlay = overlay::install(dir.c_str());
  else logf("overlay disabled (Disable = overlay)");

  HMODULE player = GetModuleHandleW(L"UnityPlayer.dll");
  g_slot = player ? hook::iat_slot(player, "KERNEL32.dll", "GetProcAddress") : nullptr;
  if (!g_slot) {
    logf("UnityPlayer.dll %s - the mod stays out of the game",
         player ? "does not import GetProcAddress from KERNEL32.dll" : "is not loaded");
    return;
  }
  if (HMODULE ga = GetModuleHandleW(L"GameAssembly.dll")) {
    // Not the way the game loads the mod (as UnityPlayer's import, before the
    // game's code): a runtime that is up already will not look il2cpp_init up
    // again, and DllMain is no place to start the mod in.
    auto corlib = reinterpret_cast<void* (*)()>(GetProcAddress(ga, "il2cpp_get_corlib"));
    if (corlib && corlib()) {
      logf("the IL2CPP runtime was up before the mod was loaded - the mod stays out of the game");
      return;
    }
  }
  take_slot("as the game starts");
  if (test_dir && *test_dir && _wgetenv(L"PPCC_TEST_LOADER") && hook::write_slot(g_slot, reinterpret_cast<void*>(&stand_in_get_proc_address)))
    logf("testkit: a stand-in mod loader takes UnityPlayer.dll's GetProcAddress after the mod");
  // Taken again as GameAssembly.dll loads, in front of whatever another loader's
  // DllMain put there since.
  auto reg = reinterpret_cast<LdrRegisterFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
  void* cookie = nullptr;
  if (!reg || reg(0, &on_dll_loaded, nullptr, &cookie) != 0)
    logf("no notice of GameAssembly.dll's load (LdrRegisterDllNotification) - a mod loader beside the mod may be "
         "the only one to start");
}

}  // namespace
}  // namespace ppcc

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    ppcc::proxy::load();
    ppcc::on_load();
  }
  return TRUE;
}
