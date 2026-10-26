// PPCabbyCodes.ini: written with its documented defaults when missing, read
// back, and saved with only the changed values replaced - the comments, the
// order and any line the mod does not know stay as they were, and new keys
// (the lock's) are added at the end. The folder's name is not ASCII (a Windows
// path goes to the system in UTF-16), and the save replaces the file there (a
// plain rename on Windows refuses an existing target). The same test runs as
// tests/build/win/test_config.exe under Wine.
//
//   make test
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "config.h"
#include "log.h"

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

static std::filesystem::path g_dir;

// The ini's text, read by the folder's own (wide on Windows) name.
static std::string slurp(const char* name) {
  std::ifstream f(g_dir / name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int main() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path name(u8"ppcc-config-\u00e9t\u00e9-");
  name += std::to_string(stamp);
  g_dir = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  if (!std::filesystem::create_directories(g_dir, ec)) return 2;
  const std::u8string dir8 = (g_dir / "").u8string();
  const std::string dir(dir8.begin(), dir8.end());  // UTF-8, as the mod has its folder
  const char* ini = "PPCabbyCodes.ini";

  ppcc::config::load(dir.c_str());
  const std::string defaults = slurp(ini);
  check(defaults.find("LockInspectorMaterials = 0") != std::string::npos, "a missing ini is written with its defaults");
  check(defaults.find("# PPCabbyCodes") == 0, "the defaults start with their documentation");
  check(!ppcc::config::get().lock_materials && !ppcc::config::get().pause_after_failure, "the switches start off");
  check(ppcc::config::get().toggle_key == 288, "ToggleKey F7 is Unity's KeyCode.F7 (288)");

  // A line of the user's own, which a save must keep where it is.
  {
    std::ofstream f(g_dir / ini, std::ios::app);
    f << "# my note\n";
  }
  ppcc::config::edit().lock_materials = true;
  ppcc::config::edit().pause_after_failure = true;
  ppcc::config::set_value("LockRules", "60.0,-23.0,1,2,map");
  check(ppcc::config::save(), "save succeeds");
  const std::string saved = slurp(ini);
  check(saved.find("LockInspectorMaterials = 1") != std::string::npos, "a switch's new value is written");
  check(saved.find("PauseAfterFailure = 1") != std::string::npos, "a second switch's too");
  check(saved.find("CrossOffCriminals = 0") != std::string::npos, "an unchanged switch keeps its value");
  check(saved.find("# my note\n") != std::string::npos, "a line the mod does not know is kept");
  check(saved.find("LockRules = 60.0,-23.0,1,2,map") != std::string::npos, "a new key is added");
  check(saved.find("# PPCabbyCodes") == 0, "the documentation stays at the top");
  check(saved.find("LockRules") > saved.find("# my note"), "new keys go at the end");

  check(ppcc::config::key_code("F1") == 282 && ppcc::config::key_code("f12") == 293, "F-keys map to KeyCodes");
  check(ppcc::config::key_code("Insert") == 277 && ppcc::config::key_code("0x120") == 288, "names and numbers too");
  check(ppcc::config::key_code("nonsense") == -1, "an unknown key name is refused");

  std::filesystem::remove_all(g_dir, ec);
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
