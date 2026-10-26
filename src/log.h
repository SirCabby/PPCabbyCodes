#pragma once

// Minimal file logger. The mod runs inside the game, where stdout goes to
// Unity's Player.log at best and a debugger is awkward, so a log file in the
// game folder is the primary diagnostic channel.
namespace ppcc {

// Called once, from the library's constructor, with the directory the log goes
// in (with a trailing separator).
void log_init(const char* dir);

// mingw's own printf on Windows (__USE_MINGW_ANSI_STDIO: %zu and the rest of C99),
// glibc's on Linux.
#ifdef _WIN32
void logf(const char* fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
#else
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif

// The thread the game's frames run on (Unity's main thread), set once the mod
// knows it; lines from any other thread are tagged with their thread id.
void log_set_main_thread();

}  // namespace ppcc
