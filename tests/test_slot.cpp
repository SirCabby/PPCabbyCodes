// hook::iat_slot, write_slot and slot_in_front (src/hook.h), as the Windows
// build uses them on UnityPlayer.dll's GetProcAddress import, which MelonLoader
// and BepInEx's Doorstop hook too - each with a function that answers
// il2cpp_init itself and asks Windows' own for the rest, never what the slot
// held. A loader's hook is in the slot before the mod, or overwrites the mod's
// after it and is put behind it again: either way the mod's hook is asked first
// and wraps the loader's answer. This program's own import of GetProcAddress
// stands in for UnityPlayer's.
//
//   make test   (tests/build/win/test_slot.exe, under Wine)
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "hook.h"

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
static GetProcAddressFn g_windows = nullptr;  // what the slot held at the start: Windows' own
static void* g_next = nullptr;                // what the mod calls on to
static int g_loader_calls = 0, g_mod_calls = 0;

static int loader_init(const char*) { return 1; }

// A loader's hook: il2cpp_init is its own, the rest Windows' own.
static FARPROC WINAPI loader_hook(HMODULE m, LPCSTR name) {
  ++g_loader_calls;
  if (std::strcmp(name, "il2cpp_init") == 0) return reinterpret_cast<FARPROC>(&loader_init);
  return g_windows(m, name);
}

// The mod's: calls on to what it found in the slot.
static FARPROC WINAPI mod_hook(HMODULE m, LPCSTR name) {
  ++g_mod_calls;
  return reinterpret_cast<GetProcAddressFn>(g_next)(m, name);
}

int main() {
  HMODULE self = GetModuleHandleA(nullptr);
  void** slot = ppcc::hook::iat_slot(self, "KERNEL32.dll", "GetProcAddress");
  check(slot != nullptr, "the program's import of GetProcAddress is found");
  if (!slot) return 1;
  g_windows = reinterpret_cast<GetProcAddressFn>(*slot);
  check(!ppcc::hook::iat_slot(self, "KERNEL32.dll", "NoSuchFunction") &&
            !ppcc::hook::iat_slot(self, "NOSUCH.dll", "GetProcAddress"),
        "an import the program does not have is not found");
  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  // A call as the module makes it: through the slot.
  auto ask = [&](const char* name) { return reinterpret_cast<GetProcAddressFn>(*slot)(kernel32, name); };
  const FARPROC sleep = g_windows(kernel32, "Sleep");

  // The loader's DllMain first, the mod's after.
  check(ppcc::hook::write_slot(slot, reinterpret_cast<void*>(&loader_hook)), "a loader's hook goes in");
  check(ppcc::hook::slot_in_front(slot, reinterpret_cast<void*>(&mod_hook), &g_next) &&
            g_next == reinterpret_cast<void*>(&loader_hook),
        "the mod goes in front of it, and calls on to it");
  g_loader_calls = g_mod_calls = 0;
  check(ask("il2cpp_init") == reinterpret_cast<FARPROC>(&loader_init) && g_mod_calls == 1 && g_loader_calls == 1,
        "a lookup asks the mod, then the loader, whose answer stands");
  check(ask("Sleep") == sleep, "any other name is Windows' answer, through both");
  check(!ppcc::hook::slot_in_front(slot, reinterpret_cast<void*>(&mod_hook), &g_next) &&
            g_next == reinterpret_cast<void*>(&loader_hook),
        "taking the slot again changes nothing while the mod is in front");

  // The loader's DllMain after the mod's: it overwrites the slot, and the mod
  // is put in front again (as GameAssembly.dll loads, in the game).
  check(ppcc::hook::write_slot(slot, reinterpret_cast<void*>(&loader_hook)), "a loader's hook overwrites the mod's");
  g_loader_calls = g_mod_calls = 0;
  ask("il2cpp_init");
  check(g_mod_calls == 0 && g_loader_calls == 1, "the mod is not asked then");
  check(ppcc::hook::slot_in_front(slot, reinterpret_cast<void*>(&mod_hook), &g_next) &&
            g_next == reinterpret_cast<void*>(&loader_hook),
        "taken again, the mod is in front of the loader");
  g_loader_calls = g_mod_calls = 0;
  check(ask("il2cpp_init") == reinterpret_cast<FARPROC>(&loader_init) && g_mod_calls == 1 && g_loader_calls == 1,
        "and asked first again");

  check(ppcc::hook::write_slot(slot, reinterpret_cast<void*>(g_windows)) &&
            reinterpret_cast<GetProcAddressFn>(*slot) == g_windows,
        "the slot is given back");
  check(!ppcc::hook::slot_in_front(nullptr, reinterpret_cast<void*>(&mod_hook), &g_next), "no slot: nothing done");
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
