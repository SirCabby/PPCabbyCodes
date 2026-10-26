#include "lock.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "config.h"
#include "hook.h"
#include "log.h"
#include "pp.h"

namespace ppcc::lock {
namespace {

using il2cpp::get;
using pp::F;
using pp::M;
using pp::Object;

constexpr int kItems = 2;
const char* const kPaperIds[kItems] = {"Rules", "Bulletin"};
const char* const kNames[kItems] = {"rulebook", "bulletin"};
const char* const kIniKeys[kItems] = {"LockRules", "LockBulletin"};

// Where an item is locked: its desk item's position (the booth's coordinates)
// and placement (on the desk, or small on the counter), and its page.
struct Saved {
  bool have = false;
  double x = 0, y = 0;
  int placement = pp::kInner;
  int page = 0;
  std::string page_id;
};
Saved g_saved[kItems];

Object* g_booth = nullptr;            // the booth of the last frame
Object* g_placed[kItems] = {};        // the papers already put in place in that booth
Object* g_held[kItems] = {};          // desk items the drag hook keeps still (this frame)
bool g_was_on = false;
bool g_capture_pending[kItems] = {};  // lock this one where it is, once it is settled
bool g_hooked = false;

using UpdateDragFn = void (*)(Object*, double, const il2cpp::Method*);
UpdateDragFn g_update_drag = nullptr;

bool held(Object* desk) { return desk && (desk == g_held[0] || desk == g_held[1]); }

void hooked_update_drag(Object* desk, double dt, const il2cpp::Method* method) {
  if (held(desk)) return;  // locked: the drag moves nothing
  g_update_drag(desk, dt, method);
}

// When a traveler is called the game sends the bulletin back to its dock on
// the counter (through a Haxe closure over returnToDock, so no call site to
// see); a locked item stays.
using ReturnToDockFn = void (*)(Object*, const il2cpp::Method*);
ReturnToDockFn g_return_to_dock = nullptr;
void hooked_return_to_dock(Object* desk, const il2cpp::Method* method) {
  if (held(desk)) return;
  g_return_to_dock(desk, method);
}

std::string serialize(const Saved& s) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%.1f,%.1f,%d,%d,%s", s.x, s.y, s.placement, s.page, s.page_id.c_str());
  return buf;
}

bool parse(const std::string& v, Saved* s) {
  char id[128] = {};
  Saved r;
  const int n = std::sscanf(v.c_str(), "%lf,%lf,%d,%d,%127[^\n]", &r.x, &r.y, &r.placement, &r.page, id);
  if (n < 4 || r.placement < 0 || r.placement > 3) return false;
  r.page_id = n == 5 ? id : "";
  r.have = true;
  *s = r;
  return true;
}

std::string page_id_of(Object* paper) {
  if (!M.paper_page_id) return {};
  auto fn = reinterpret_cast<Object* (*)(Object*, const il2cpp::Method*)>(M.paper_page_id->pointer);
  return pp::str(fn(paper, M.paper_page_id));
}

bool steady(Object* desk) {
  return desk && pp::enum_index(get<Object*>(desk, F.desk_state)) == pp::kNormal && !get<bool>(desk, F.desk_dragging);
}

void capture(int i, Object* paper) {
  Object* desk = pp::paper_desk(paper);
  const pp::Pos pos = pp::ent_pos(desk);
  Saved& s = g_saved[i];
  s.have = true;
  s.x = pos.x;
  s.y = pos.y;
  s.placement = pp::enum_index(get<Object*>(desk, F.desk_placement));
  s.page = get<int32_t>(paper, F.paper_page);
  s.page_id = page_id_of(paper);
  config::set_value(kIniKeys[i], serialize(s));
  logf("lock: %s locked at %.1f,%.1f (placement %d), page %d \"%s\"", kNames[i], s.x, s.y, s.placement, s.page,
       s.page_id.c_str());
}

// Puts a desk item where it is locked.
void put(int i, Object* desk) {
  const Saved& s = g_saved[i];
  if (pp::enum_index(get<Object*>(desk, F.desk_placement)) != s.placement) {
    if (Object* value = pp::placement_value(s.placement)) {
      auto set_placement = reinterpret_cast<Object* (*)(Object*, Object*, const il2cpp::Method*)>(
          M.desk_set_placement->pointer);
      set_placement(desk, value, M.desk_set_placement);
    }
  }
  pp::set_ent_pos(desk, pp::Pos{s.x, s.y});
  reinterpret_cast<void (*)(Object*, const il2cpp::Method*)>(M.desk_clip_rects->pointer)(desk, M.desk_clip_rects);
}

bool out_of_place(int i, Object* desk) {
  const Saved& s = g_saved[i];
  const pp::Pos pos = pp::ent_pos(desk);
  return pos.x != s.x || pos.y != s.y || pp::enum_index(get<Object*>(desk, F.desk_placement)) != s.placement;
}

// Puts a paper where its item is locked and opens it at the locked page.
void place(int i, Object* paper) {
  const Saved& s = g_saved[i];
  put(i, pp::paper_desk(paper));

  int page = -1;
  if (!s.page_id.empty()) {
    Object* def = pp::paper_def(paper);
    auto find = reinterpret_cast<int32_t (*)(Object*, il2cpp::String*, const il2cpp::Method*)>(M.def_find_page->pointer);
    page = find(def, pp::new_string(s.page_id), M.def_find_page);
  }
  if (page < 0) page = s.page;  // a page id this day's paper lacks: the same page number
  if (page != get<int32_t>(paper, F.paper_page)) {
    auto set_page = reinterpret_cast<void (*)(Object*, int32_t, const il2cpp::Method*)>(M.paper_set_page->pointer);
    set_page(paper, page, M.paper_set_page);
  }
  logf("lock: %s put at %.1f,%.1f, page %d (asked %d \"%s\")", kNames[i], s.x, s.y, get<int32_t>(paper, F.paper_page),
       page, s.page_id.c_str());
}

}  // namespace

bool start() {
  for (int i = 0; i < kItems; ++i) {
    if (parse(config::value(kIniKeys[i]), &g_saved[i]))
      logf("lock: saved %s lock %.1f,%.1f page %d \"%s\"", kNames[i], g_saved[i].x, g_saved[i].y, g_saved[i].page,
           g_saved[i].page_id.c_str());
  }
  if (!M.desk_update_drag || !M.paper_set_page || !M.desk_set_placement || !M.desk_clip_rects || !M.def_find_page)
    return false;
  g_hooked = hook::inline_hook(M.desk_update_drag->pointer, reinterpret_cast<void*>(&hooked_update_drag),
                               reinterpret_cast<void**>(&g_update_drag), "DeskItem.updateCustomDrag");
  if (g_hooked && M.desk_return_dock)
    hook::inline_hook(M.desk_return_dock->pointer, reinterpret_cast<void*>(&hooked_return_to_dock),
                      reinterpret_cast<void**>(&g_return_to_dock), "DeskItem.returnToDock");
  return g_hooked;
}

void tick(Object* booth, bool on) {
  if (booth != g_booth) {
    g_booth = booth;
    for (Object*& p : g_placed) p = nullptr;
    // A capture still pending when its booth goes (an item that never settled
    // there) is dropped: that item keeps its last lock.
    for (bool& p : g_capture_pending) p = false;
  }
  for (Object*& d : g_held) d = nullptr;
  if (!g_hooked || !booth) {
    g_was_on = on;
    return;
  }
  // Switching on in the booth locks them where they are now - each as soon as
  // it is settled (not mid-drag, not still arriving).
  if (on && !g_was_on)
    for (bool& p : g_capture_pending) p = true;
  g_was_on = on;
  if (!on) return;
  Object* papers[kItems] = {pp::find_paper(booth, kPaperIds[0]), pp::find_paper(booth, kPaperIds[1])};
  bool changed = false;
  for (int i = 0; i < kItems; ++i) {
    Object* paper = papers[i];
    if (!paper) continue;
    Object* desk = pp::paper_desk(paper);
    if (g_capture_pending[i] || !g_saved[i].have) {
      // To be locked where it is (on with nothing saved yet: where it first settles).
      if (steady(desk)) {
        capture(i, paper);
        g_placed[i] = paper;
        g_capture_pending[i] = false;
        changed = true;
      }
      continue;
    }
    g_held[i] = desk;
    if (!steady(desk)) continue;
    if (paper != g_placed[i]) {
      place(i, paper);
      g_placed[i] = paper;
    } else if (out_of_place(i, desk)) {
      // Something of the game's moved it after all: it goes back.
      const pp::Pos was = pp::ent_pos(desk);
      put(i, desk);
      logf("lock: %s was moved to %.1f,%.1f - put back", kNames[i], was.x, was.y);
    }
  }
  if (changed) config::save();
}

// A page as the panel names it: the game's page ids, read the way they print.
std::string page_name(const Saved& s) {
  const std::string& id = s.page_id;
  if (id.empty()) return "page " + std::to_string(s.page + 1);
  if (id == "home") return "the contents";
  if (id == "criminals") return "the wanted criminals";
  if (id.rfind("notice", 0) == 0 && id.size() > 6 && std::isdigit(static_cast<unsigned char>(id[6])))
    return "notice " + std::to_string(std::atoi(id.c_str() + 6) + 1);
  return "the " + id + " page";
}

std::string describe() {
  std::string out;
  for (int i = 0; i < kItems; ++i) {
    if (!g_saved[i].have) continue;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%s%s: %s, at %s", out.empty() ? "" : "\n", kNames[i],
                  g_saved[i].placement == pp::kOuter ? "on the counter" : "on the desk",
                  page_name(g_saved[i]).c_str());
    out += buf;
  }
  if (!out.empty()) return out;
  return g_booth ? "Nothing locked yet: they lock where they are as soon as they settle."
                 : "Nothing locked yet: they lock where they lie when the next day starts.";
}

}  // namespace ppcc::lock
