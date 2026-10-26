// platform::guarded (src/platform.h), which the mod's frame runs inside: a
// managed exception out of a game call the mod makes must stop there, and the
// mod's frame stop for good, not take the game's frame down with it. On Linux
// IL2CPP's managed exceptions are C++ exceptions, which the mod's catch sees; on
// Windows they are MSVC's (0xE06D7363), which mingw's catch does not: an SEH
// handler of the mod's own catches them. Here the "game" throws as its code
// does on each - a C++ throw, or RaiseException as MSVC's _CxxThrowException
// does - from below a frame of the mod's with a destructor, as the tick's are.
// The same test runs as tests/build/win/test_guard.exe under Wine.
//
//   make test
#include <cstdio>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

#include "platform.h"

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// How the game's code throws a managed exception.
__attribute__((noinline)) static void game_throws() {
#ifdef _WIN32
  ULONG_PTR args[4] = {0x19930520, 0, 0, 0};  // MSVC's magic number, the object, its throw info, its module
  RaiseException(0xE06D7363, EXCEPTION_NONCONTINUABLE, 4, args);
#else
  throw std::runtime_error("a managed exception");
#endif
}

static int g_after = 0;

static void returns(void* p) { ++*static_cast<int*>(p); }

static void through_mod_frame(void*) {
  std::string held(64, 'x');  // the mod's own frame, with a destructor
  game_throws();
  g_after += static_cast<int>(held.size());  // not reached
}

static void mod_throws(void*) { throw std::runtime_error("the mod's own"); }

static void inner_guard(void* p) {
  *static_cast<bool*>(p) = ppcc::platform::guarded([](void*) { game_throws(); }, nullptr);
}

int main() {
  int n = 0;
  check(ppcc::platform::guarded(returns, &n) && n == 1, "a call that returns: true, and it ran");
  check(!ppcc::platform::guarded(through_mod_frame, nullptr) && g_after == 0,
        "the game's exception, through a frame of the mod's: false, and nothing after the throw ran");
  check(!ppcc::platform::guarded(mod_throws, nullptr), "the mod's own C++ exception: false");
  check(ppcc::platform::guarded(returns, &n) && n == 2, "a call after those: true, and it ran");
  bool inner = true;
  check(ppcc::platform::guarded(inner_guard, &inner) && !inner,
        "a guard inside a guard keeps the exception to itself");
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
