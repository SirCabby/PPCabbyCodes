#include "log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

#include "platform.h"

namespace ppcc {
namespace {

// Made on first use (the library's constructor logs before other files'
// globals are initialised) and never destroyed (a hook on another thread may log
// while the process exits and runs the static destructors).
std::string& path() {
  static std::string* p = new std::string;
  return *p;
}
std::mutex& lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::atomic<long> g_main_tid{0};

}  // namespace

void log_init(const char* dir) {
  std::lock_guard<std::mutex> guard(lock());
  std::string& g_path = path();
  if (!g_path.empty()) return;
  g_path = std::string(dir) + "PPCabbyCodes.log";

  // Truncate on each launch so the log always describes the current run - but
  // keep the run before it, because the first thing that happens after a crash
  // is a relaunch, and that would otherwise wipe the only record of it.
  const std::string previous = std::string(dir) + "PPCabbyCodes.prev.log";
  platform::replace_file(g_path, previous);
  if (FILE* f = platform::open_file(g_path, "w")) std::fclose(f);
}

void log_set_main_thread() { g_main_tid.store(platform::thread_id()); }

void logf(const char* fmt, ...) {
  std::lock_guard<std::mutex> guard(lock());
  const std::string& g_path = path();
  if (g_path.empty()) return;
  FILE* f = platform::open_file(g_path, "a");
  if (!f) return;

  const platform::Time t = platform::local_time();
  std::fprintf(f, "[%02d:%02d:%02d.%03d] ", t.hour, t.minute, t.second, t.millisecond);
  const long tid = platform::thread_id();
  const long main_tid = g_main_tid.load();
  if (main_tid != 0 && tid != main_tid) std::fprintf(f, "(thread %ld) ", tid);

  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);

  std::fputc('\n', f);
  std::fclose(f);  // flush-per-line: a crash must not lose the last message
}

}  // namespace ppcc
