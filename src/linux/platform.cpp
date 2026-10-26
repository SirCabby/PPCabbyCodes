#include "platform.h"

#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <ctime>

namespace ppcc::platform {

long thread_id() { return static_cast<long>(syscall(SYS_gettid)); }

Time local_time() {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  tm t{};
  localtime_r(&ts.tv_sec, &t);
  return Time{t.tm_hour, t.tm_min, t.tm_sec, static_cast<int>(ts.tv_nsec / 1000000)};
}

FILE* open_file(const std::string& path, const char* mode) { return std::fopen(path.c_str(), mode); }

bool replace_file(const std::string& from, const std::string& to) {
  return std::rename(from.c_str(), to.c_str()) == 0;
}

bool remove_file(const std::string& path) { return std::remove(path.c_str()) == 0; }

void* symbol(void* library, const char* name) { return dlsym(library, name); }

bool guarded(void (*fn)(void*), void* arg) {
  try {
    fn(arg);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace ppcc::platform
