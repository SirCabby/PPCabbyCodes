#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Code hooks for x86-64: inline hooks on a function's first instructions
// (hook.cpp), and hooks on a module's imports - its GOT on Linux, its import
// address table on Windows (linux/hook_linux.cpp, win/hook_win.cpp).
namespace ppcc::hook {

// Replaces the first instructions of `target` with a jump to `detour` and
// hands back, in *original, a trampoline that runs those instructions and
// continues in `target`: the function as it was. The jump goes through a relay
// placed within 2 GB of `target` (so the patch is a 5-byte rel32 jmp, however
// far away the mod was loaded). Install only while no thread can be running
// the first 5 bytes of `target` (the mod hooks before the game runs them).
// False, with the reason in the log, when the prologue holds an instruction
// the relocator does not handle; the function is then left untouched.
bool inline_hook(void* target, void* detour, void** original, const char* name);

// The trampoline building on its own, for tests: relocates the instructions at
// `target` covering at least `min_len` bytes into `out` (which must be within
// 2 GB of `target`) followed by a jump back. Returns the trampoline's length
// (0 on failure, the reason in *why) and the bytes taken in *stolen.
size_t build_trampoline(const uint8_t* target, size_t min_len, uint8_t* out, size_t cap, size_t* stolen,
                        std::string* why);

// Memory for code within 2 GB of `around` (read/write/execute until sealed with
// seal_code), and its release. Per platform.
uint8_t* alloc_code_near(uintptr_t around, size_t size);
bool seal_code(uint8_t* p, size_t size);
void free_code(uint8_t* p, size_t size);

// Writes `n` bytes over code at `at` (made writable for the write, its
// protection put back, the instruction cache flushed). Per platform.
bool patch_code(void* at, const void* bytes, size_t n);

#ifdef _WIN32
// The import address table slot through which `module` calls `dll`!`symbol`
// (the DLL's name as the module imports it, any case), or null.
void** iat_slot(void* module, const char* dll, const char* symbol);

// Writes a pointer into a read-only slot (an import address table's, a class's
// vtable): made writable for the write, its protection put back.
bool write_slot(void** slot, void* value);

// Puts `hook` in the slot in front of whatever is there, unless it is there
// already: *next gets what the slot held (the function `hook` calls on to).
// True when it changed the slot. Another hooker that overwrites the slot later
// without calling on (MelonLoader's and BepInEx's loaders, for GetProcAddress)
// is put behind the hook again by a second call.
bool slot_in_front(void** slot, void* hook, void** next);
#else
// A loaded module, found by the end of its path ("UnityPlayer.so").
struct Module {
  uintptr_t base = 0;
  std::string path;
};
bool find_module(const char* file_name, Module* out);

// Replaces the GOT slot through which the module whose path ends in
// `module_file` calls `symbol` (PLT and GLOB_DAT relocations alike). *previous
// gets what the slot held - under lazy binding that may be the PLT resolver
// stub, so use dlsym for the real function. False when the module or the
// import is not there.
bool got_hook(const char* module_file, const char* symbol, void* replacement, void** previous);
#endif

}  // namespace ppcc::hook
