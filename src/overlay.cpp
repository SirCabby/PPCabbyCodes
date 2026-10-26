#include "overlay.h"

#include <atomic>
#include <chrono>
#include <cfloat>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "panel.h"

namespace ppcc::overlay {
namespace {

// Made on first use and never destroyed (ImGui keeps the ini path's pointer; the
// render thread reads the input until the process is gone).
std::string& ini_path() {
  static std::string* p = new std::string;
  return *p;
}
std::chrono::steady_clock::time_point g_last_frame;

std::mutex& input_lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
std::vector<InputSample>& samples() {  // published since the last panel frame
  static std::vector<InputSample>* v = new std::vector<InputSample>;
  return *v;
}
bool g_buttons_down[3] = {};
std::atomic<bool> g_wants_mouse{false};

// The panel grows with the window: the game draws at 570x320 scaled up, so its
// usual 2x window is 640 tall - scale 1 there, more on a bigger window.
float g_scale = 0.0f;
void apply_scale(unsigned h) {
  float want = static_cast<float>(h) / 640.0f;
  want = want < 1.0f ? 1.0f : (want > 4.0f ? 4.0f : want);
  if (want > g_scale - 0.01f && want < g_scale + 0.01f) return;
  ImGuiStyle fresh;
  ImGui::StyleColorsDark(&fresh);
  fresh.WindowRounding = 4.0f;
  fresh.FrameRounding = 3.0f;
  fresh.ScaleAllSizes(want);
  fresh.FontScaleDpi = want;
  ImGui::GetStyle() = fresh;
  g_scale = want;
}

void feed_input(ImGuiIO& io, unsigned w, unsigned h) {
  std::vector<InputSample> taken;
  {
    std::lock_guard<std::mutex> guard(input_lock());
    taken.swap(samples());
  }
  for (const InputSample& s : taken) {
    // Unity's screen and the back buffer are the same size; scale in case not.
    const float sx = s.screen_w > 0 ? static_cast<float>(w) / static_cast<float>(s.screen_w) : 1.0f;
    const float sy = s.screen_h > 0 ? static_cast<float>(h) / static_cast<float>(s.screen_h) : 1.0f;
    if (s.x >= 0 && s.y >= 0) io.AddMousePosEvent(s.x * sx, s.y * sy);
    else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    for (int b = 0; b < 3; ++b) {
      if (s.buttons[b] != g_buttons_down[b]) {
        io.AddMouseButtonEvent(b, s.buttons[b]);
        g_buttons_down[b] = s.buttons[b];
      }
    }
    if (s.wheel != 0) io.AddMouseWheelEvent(0, s.wheel);
  }
}

}  // namespace

bool install(const char* dir) {
  ini_path() = std::string(dir) + "PPCabbyCodes.imgui.ini";
  return install_presents();
}

void create_context() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = ini_path().c_str();
  // The context is never destroyed, so the ini is only written by this timer: a
  // second keeps a fold or a move made just before the game quits.
  io.IniSavingRate = 1.0f;
  io.BackendPlatformName = "ppcc_unity";
  io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
  g_last_frame = std::chrono::steady_clock::now();
}

ImDrawData* frame(unsigned w, unsigned h, void (*renderer_new_frame)()) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
  const auto now = std::chrono::steady_clock::now();
  float dt = std::chrono::duration<float>(now - g_last_frame).count();
  g_last_frame = now;
  io.DeltaTime = dt > 0.0f ? (dt < 0.5f ? dt : 0.5f) : 1.0f / 1000.0f;
  feed_input(io, w, h);
  apply_scale(h);
  // The game draws its own cursor, under the panel: over the panel, ImGui's.
  io.MouseDrawCursor = g_wants_mouse.load();

  renderer_new_frame();
  ImGui::NewFrame();
  panel::draw();
  ImGui::Render();
  g_wants_mouse.store(io.WantCaptureMouse);

  ImDrawData* dd = ImGui::GetDrawData();
  bool textures = false;
  if (dd && dd->Textures)
    for (ImTextureData* t : *dd->Textures) textures |= t->Status != ImTextureStatus_OK;
  if (!dd || (dd->CmdListsCount == 0 && !textures)) return nullptr;
  return dd;
}

void publish_input(const InputSample& s) {
  std::lock_guard<std::mutex> guard(input_lock());
  if (samples().size() < 256) samples().push_back(s);
}

bool wants_mouse() { return g_wants_mouse.load(); }

}  // namespace ppcc::overlay
