#include "panel.h"

#include <atomic>
#include <cfloat>
#include <mutex>

#include "imgui.h"
#include "switches.h"
#include "version.h"

namespace ppcc::panel {
namespace {

// Made on first use and never destroyed: the render thread draws from them
// until the process is gone.
std::mutex& model_lock() {
  static std::mutex* m = new std::mutex;
  return *m;
}
Model& model() {
  static Model* m = new Model;
  return *m;
}
std::atomic<unsigned> g_requests{0};
std::atomic<bool> g_continue_shown{false};
std::atomic<float> g_continue_x{0}, g_continue_y{0};

void request(unsigned r) { g_requests.fetch_or(r); }

// A switch's checkbox; a change is applied and saved by the game's thread.
void switch_box(const char* label, switches::Kind k, bool available, const char* help) {
  bool v = switches::get(k);
  if (!available) ImGui::BeginDisabled();
  if (ImGui::Checkbox(label, &v)) {
    switches::on[k].store(v);
    request(kRequestSave);
  }
  if (!available) ImGui::EndDisabled();
  if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal) &&
      ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
    ImGui::TextUnformatted(help);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void note(const std::string& text) {
  if (text.empty()) return;
  ImGui::Indent();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
  ImGui::Unindent();
}

void draw_menu(const Model& m) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float em = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + em, vp->WorkPos.y + em), ImGuiCond_FirstUseEver);
  // A fixed width (the height follows the contents) so the notes wrap at a readable length.
  ImGui::SetNextWindowSizeConstraints(ImVec2(em * 24, 0), ImVec2(em * 24, FLT_MAX));
  ImGui::SetNextWindowBgAlpha(0.92f);
  // The title bar's arrow (or a double-click on the title) folds the panel down to
  // its title; ImGui keeps that in PPCabbyCodes.imgui.ini with the window's place.
  if (!ImGui::Begin("PPCabbyCodes " PPCC_VERSION "###PPCabbyCodes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }
  if (!m.ready) {
    ImGui::TextWrapped("The mod could not hook the game: %s", m.problem.empty() ? "see PPCabbyCodes.log" : m.problem.c_str());
    ImGui::End();
    return;
  }
  switch_box("Lock Inspector Materials", switches::kLockMaterials, m.feature_ok[switches::kLockMaterials],
             "The rulebook and the bulletin stay where they are on the desk: dragging them does not move "
             "them. Each new day puts them back there, open at the pages they had when locked. You can "
             "still turn their pages. To move them, or to change those pages, untick this, arrange them "
             "and tick it again.");
  if (switches::get(switches::kLockMaterials)) note(m.lock_text);
  switch_box("Cross-Off Criminals", switches::kCrossOffCriminals, m.feature_ok[switches::kCrossOffCriminals],
             "The bulletin's wanted criminals who have already been found lose their photos.");
  if (switches::get(switches::kCrossOffCriminals)) note(m.criminals_text);
  switch_box("Pause game after failure", switches::kPauseAfterFailure, m.feature_ok[switches::kPauseAfterFailure],
             "After a mistake the entrant's papers stay on the desk and the clock stops, so you can see "
             "what went wrong; the citation still prints. Continue (on screen) carries on as the game "
             "meant to.");
  if (!m.problem.empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", m.problem.c_str());
  }
  ImGui::Separator();
  ImGui::TextDisabled("%s hides this panel", m.toggle_key.c_str());
  ImGui::End();
}

void draw_continue(const Model& m) {
  // Top right, over the border view: clear of the papers, the printer and an
  // endless run's scoreboard (top centre).
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float em = ImGui::GetFontSize();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - em, vp->WorkPos.y + em), ImGuiCond_Always,
                          ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("###PPCabbyCodesContinue", nullptr, flags)) {
    if (!m.hold_text.empty()) {
      ImGui::TextUnformatted(m.hold_text.c_str());
      ImGui::SameLine();
    }
    if (ImGui::Button("Continue")) request(kRequestContinue);
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    g_continue_x.store((lo.x + hi.x) * 0.5f);
    g_continue_y.store((lo.y + hi.y) * 0.5f);
    g_continue_shown.store(true);
  }
  ImGui::End();
}

}  // namespace

void publish(const Model& m) {
  std::lock_guard<std::mutex> guard(model_lock());
  model() = m;
}

unsigned take_requests() { return g_requests.exchange(0); }

void draw() {
  Model m;
  {
    std::lock_guard<std::mutex> guard(model_lock());
    m = model();
  }
  if (m.show_menu) draw_menu(m);
  if (m.holding && !m.paused) draw_continue(m);
  else g_continue_shown.store(false);
}

bool continue_button_center(float* x, float* y) {
  if (!g_continue_shown.load()) return false;
  *x = g_continue_x.load();
  *y = g_continue_y.load();
  return true;
}

}  // namespace ppcc::panel
