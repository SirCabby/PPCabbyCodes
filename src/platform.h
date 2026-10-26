#pragma once

#include <cstdio>
#include <string>

// What differs between the Linux library and the Windows DLL beneath the mod's
// own code: thread ids, the wall clock, files (Windows names them in UTF-16), a
// loaded library's exports, and catching what a call into the game throws.
// linux/platform.cpp and win/platform.cpp implement it.
namespace ppcc::platform {

long thread_id();

// The wall clock, for the log's timestamps.
struct Time {
  int hour = 0, minute = 0, second = 0, millisecond = 0;
};
Time local_time();

// Files by UTF-8 path. replace_file moves `from` over `to` whether or not `to`
// exists (a plain rename refuses an existing target on Windows).
FILE* open_file(const std::string& path, const char* mode);
bool replace_file(const std::string& from, const std::string& to);
bool remove_file(const std::string& path);

// An exported function of a loaded library (dlsym, GetProcAddress).
void* symbol(void* library, const char* name);

// Runs fn(arg); false when an exception came out of it. The game's managed
// exceptions are C++ exceptions (IL2CPP's), which the mod's own code never
// throws. On Windows they are MSVC's, which mingw's catch does not see: an SEH
// handler of the mod's own catches them there, and the frames between the throw
// and this call are unwound without running the mod's destructors in them (so no
// frame of the mod's may hold a lock across a call into the game).
bool guarded(void (*fn)(void*), void* arg);

}  // namespace ppcc::platform
