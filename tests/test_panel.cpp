// The panel (src/panel.cpp) drawn by ImGui with no renderer and driven by mouse
// events, as a player drives it: an open panel's switch takes a click; the title
// bar's arrow folds the window down to its title; the fold goes into the ImGui
// ini with the window's place, and a new start that reads the ini comes up
// folded; the arrow opens it again, and a double-click on the title folds it.
//
//   make test
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "panel.h"
#include "switches.h"

namespace {

int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

void start_imgui(const char* ini_text) {
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // the ini goes through memory here
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // no renderer: the font atlas is built as asked
  io.DisplaySize = ImVec2(1140, 640);  // the game's usual window (the overlay's scale 1)
  if (ini_text) ImGui::LoadIniSettingsFromMemory(ini_text);
}

// One frame of the overlay's, minus the drawing.
void frame() {
  ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  ppcc::panel::draw();
  ImGui::Render();
}

void press(bool down) {
  ImGui::GetIO().AddMouseButtonEvent(0, down);
  frame();
}

void click(ImVec2 at) {
  ImGui::GetIO().AddMousePosEvent(at.x, at.y);
  frame();
  press(true);
  press(false);
  frame();  // a fold asked for in one frame is made by the next one's Begin
}

ImGuiWindow* panel_window() { return ImGui::FindWindowByName("###PPCabbyCodes"); }

// The title bar's fold arrow: ImGui puts it at the title bar's start, FramePadding in.
ImVec2 arrow(const ImGuiWindow* w) {
  const ImVec2 pad = ImGui::GetStyle().FramePadding;
  const float size = ImGui::GetFontSize();
  return ImVec2(w->Pos.x + pad.x + size * 0.5f, w->Pos.y + pad.y + size * 0.5f);
}

// The panel's lines in the ini, up to the next section.
std::string panel_section(const std::string& ini) {
  const size_t at = ini.find("[Window][###PPCabbyCodes]");
  if (at == std::string::npos) return {};
  const size_t end = ini.find("\n[", at + 1);
  return ini.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

}  // namespace

int main() {
  ppcc::panel::Model m;
  m.paused = true;
  m.show_menu = true;
  m.ready = true;
  for (bool& ok : m.feature_ok) ok = true;
  ppcc::panel::publish(m);

  start_imgui(nullptr);
  frame();
  frame();
  ImGuiWindow* w = panel_window();
  check(w && !w->Collapsed, "the panel comes up open");
  if (!w) return 1;
  const float open_height = w->Size.y;

  // The first switch's box is where the window's contents start.
  const float box = ImGui::GetFrameHeight();
  click(ImVec2(w->DC.CursorStartPos.x + box * 0.5f, w->DC.CursorStartPos.y + box * 0.5f));
  check(ppcc::switches::get(ppcc::switches::kLockMaterials), "an open panel's switch takes a click");
  check((ppcc::panel::take_requests() & ppcc::panel::kRequestSave) != 0, "and asks for the ini to be written");

  click(arrow(w));
  check(w->Collapsed, "the title bar's arrow folds the panel");
  check(w->Size.y <= w->TitleBarHeight + 1.0f && w->Size.y < open_height, "folded, only its title bar shows");
  check(ppcc::panel::take_requests() == 0, "folding it asks the game for nothing");

  const std::string ini = ImGui::SaveIniSettingsToMemory();
  const std::string section = panel_section(ini);
  check(section.find("Collapsed=1") != std::string::npos, "the fold is written to the ini with the window's place");
  ImGui::DestroyContext();

  // The next start: the overlay's context reads the ini the last one wrote.
  start_imgui(ini.c_str());
  frame();
  frame();
  w = panel_window();
  check(w && w->Collapsed, "a new start that reads the ini comes up folded");
  if (!w) return 1;

  click(arrow(w));
  check(!w->Collapsed && w->Size.y > w->TitleBarHeight + 1.0f, "the arrow opens it again");
  // ImGui writes the line only for a folded window.
  check(panel_section(ImGui::SaveIniSettingsToMemory()).find("Collapsed=1") == std::string::npos,
        "and the ini no longer has it folded");

  // A double-click on the title, away from the arrow.
  ImGui::GetIO().AddMousePosEvent(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->TitleBarHeight * 0.5f);
  frame();
  press(true);
  press(false);
  press(true);
  press(false);
  frame();
  check(w->Collapsed, "a double-click on the title folds it too");
  ImGui::DestroyContext();

  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
