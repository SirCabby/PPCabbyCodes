#include "criminals.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "hook.h"
#include "log.h"
#include "pp.h"

namespace ppcc::criminals {
namespace {

using il2cpp::get;
using pp::F;
using pp::M;
using pp::Object;

bool g_hooked = false;
bool g_on = false;
unsigned g_found = 0;          // criminals of today found, by index (bit i)
unsigned g_shown_mask = ~0u;   // the mask the bulletin and poster were last drawn with
Object* g_booth = nullptr;
int g_total = 0;

using GetImageFn = Object* (*)(Object*, Object*, Object*, double, int32_t, int32_t, const il2cpp::Method*);
GetImageFn g_get_image = nullptr;

// The criminal a bulletin photo slot shows: the fact's text, an index into the
// day's criminals (empty: the blank slot).
int slot_criminal(Object* env, Object* paper_id, Object* fact_id) {
  const std::string path = pp::str(paper_id) + "/" + pp::str(fact_id);
  auto text = reinterpret_cast<Object* (*)(Object*, il2cpp::String*, il2cpp::String*, const il2cpp::Method*)>(
      M.env_fact_text->pointer);
  const std::string v = pp::str(text(env, pp::new_string(path), pp::new_string(""), M.env_fact_text));
  if (v.empty()) return -1;
  char* end = nullptr;
  const long n = std::strtol(v.c_str(), &end, 10);
  return end && *end == '\0' && n >= 0 && n < 32 ? static_cast<int>(n) : -1;
}

Object* hooked_get_image(Object* env, Object* paper_id, Object* fact_id, double scale, int32_t text_color,
                         int32_t back_color, const il2cpp::Method* method) {
  if (g_on && g_found && pp::str_eq(paper_id, "Bulletin")) {
    const std::string fact = pp::str(fact_id);
    if (fact.rfind("Criminal", 0) == 0) {
      // No image: the page's own empty photo box shows (getImage answers null
      // itself for a fact with nothing to draw, so the page takes it).
      const int c = slot_criminal(env, paper_id, fact_id);
      if (c >= 0 && (g_found >> c) & 1u) return nullptr;
    }
  }
  return g_get_image(env, paper_id, fact_id, scale, text_color, back_color, method);
}

unsigned found_mask(Object* booth, int* total) {
  Object* day = get<Object*>(booth, F.booth_day);
  *total = 0;
  // Days before the wanted criminals have them in the data, but no bulletin page.
  if (!(static_cast<unsigned>(get<int32_t>(day, F.day_features)) & pp::kFeatureCriminals)) return 0;
  Object* env = get<Object*>(get<Object*>(booth, F.booth_engine), F.engine_env);
  Object* traveler = get<Object*>(env, F.env_traveler);
  const int at_window = traveler ? get<int32_t>(traveler, F.trav_criminal) : -1;
  Object* list = get<Object*>(day, F.day_criminals);
  const int n = pp::array_length(list);
  *total = n;  // the day has the feature: these are on its bulletin
  unsigned mask = 0;
  for (int i = 0; i < n && i < 32; ++i) {
    Object* c = pp::array_get(list, i);
    const int index = get<int32_t>(c, F.crim_index);
    if (c && get<bool>(c, F.crim_used) && index != at_window && index >= 0 && index < 32) mask |= 1u << index;
  }
  return mask;
}

// The bulletin's page is drawn once, when it is turned to: rebuild it so the
// photos follow. The poster's sprites are the day's criminals in order.
void redraw(Object* booth, unsigned hidden) {
  if (Object* bulletin = pp::find_paper(booth, "Bulletin")) {
    reinterpret_cast<void (*)(Object*, const il2cpp::Method*)>(M.paper_update_page->pointer)(bulletin,
                                                                                            M.paper_update_page);
  }
  Object* sprites = get<Object*>(get<Object*>(booth, F.booth_poster), F.poster_sprites);
  for (int i = 0; i < pp::array_length(sprites); ++i)
    il2cpp::set<bool>(pp::array_get(sprites, i), F.visual_visible, !((hidden >> i) & 1u));
}

}  // namespace

bool start() {
  if (!M.env_get_image || !M.env_fact_text || !M.paper_update_page) return false;
  g_hooked = hook::inline_hook(M.env_get_image->pointer, reinterpret_cast<void*>(&hooked_get_image),
                               reinterpret_cast<void**>(&g_get_image), "BoothEnv.getImage");
  return g_hooked;
}

void tick(Object* booth, bool on) {
  if (!g_hooked) return;
  if (booth != g_booth) {
    g_booth = booth;
    g_shown_mask = ~0u;
  }
  if (!booth) {
    g_found = 0;
    g_total = 0;
    return;
  }
  g_found = found_mask(booth, &g_total);
  g_on = on;
  const unsigned hidden = on ? g_found : 0;
  // A fresh booth draws its bulletin by itself (with the hook in place); redraw
  // only on a change after that.
  if (g_shown_mask == ~0u) {
    g_shown_mask = hidden;
    if (hidden) redraw(booth, hidden);
    return;
  }
  if (hidden != g_shown_mask) {
    logf("criminals: found %#x of %d - %s", g_found, g_total, on ? "crossing off" : "showing all");
    redraw(booth, hidden);
    g_shown_mask = hidden;
  }
}

std::string describe() {
  if (!g_booth || g_total == 0) return "No wanted criminals on today's bulletin.";
  int found = 0;
  for (int i = 0; i < g_total && i < 32; ++i) found += (g_found >> i) & 1u;
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%d of today's %d wanted criminals found.", found, g_total);
  return buf;
}

}  // namespace ppcc::criminals
