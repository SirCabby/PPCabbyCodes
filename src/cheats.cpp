#include "cheats.h"

#include <cstdio>
#include <string>

#include "criminals.h"
#include "failhold.h"
#include "game.h"
#include "lock.h"
#include "log.h"
#include "pp.h"
#include "switches.h"

namespace ppcc::cheats {
namespace {

using il2cpp::get;
using pp::F;
using pp::Object;

bool g_bound = false;
bool g_ok[switches::kCount] = {};
unsigned g_injected = 0;

std::string paper_line(Object* paper) {
  Object* def = pp::paper_def(paper);
  Object* desk = pp::paper_desk(paper);
  const pp::Pos pos = pp::ent_pos(desk);
  std::string page_id;
  if (pp::M.paper_page_id)
    page_id = pp::str(reinterpret_cast<Object* (*)(Object*, const il2cpp::Method*)>(pp::M.paper_page_id->pointer)(
        paper, pp::M.paper_page_id));
  // The stamps on it (StampApproval.flags), for a stampable paper the engine knows.
  int32_t approval = -1;
  if (get<bool>(def, F.def_stampable) && pp::M.engine_paper_approval) {
    Object* booth = pp::booth_of(game::current_game());
    Object* engine = get<Object*>(booth, F.booth_engine);
    if (engine) {
      Object* a = reinterpret_cast<Object* (*)(Object*, Object*, const il2cpp::Method*)>(
          pp::M.engine_paper_approval->pointer)(engine, get<Object*>(paper, F.paper_id), pp::M.engine_paper_approval);
      if (a) approval = get<int32_t>(a, F.approval_flags);
    }
  }
  char buf[400];
  std::snprintf(buf, sizeof(buf), "%s (%s) page %d \"%s\" at %.1f,%.1f placement %d state %d%s%s%s%s stamps %d",
                pp::str(get<Object*>(paper, F.paper_id)).c_str(), pp::str(get<Object*>(def, F.def_id)).c_str(),
                get<int32_t>(paper, F.paper_page), page_id.c_str(), pos.x, pos.y,
                pp::enum_index(get<Object*>(desk, F.desk_placement)), pp::enum_index(get<Object*>(desk, F.desk_state)),
                get<bool>(desk, F.desk_dragging) ? " dragging" : "", get<bool>(desk, F.desk_visible) ? "" : " invisible",
                get<bool>(def, F.def_stampable) ? " stampable" : "", get<bool>(def, F.def_from_traveler) ? " from-traveler" : "",
                approval);
  return buf;
}

}  // namespace

void start() {
  g_bound = pp::bind();
  if (!g_bound) {
    logf("cheats: the game is not as the mod knows it - the switches stay off");
    return;
  }
  g_ok[switches::kLockMaterials] = lock::start();
  g_ok[switches::kCrossOffCriminals] = criminals::start();
  g_ok[switches::kPauseAfterFailure] = failhold::start();
  logf("cheats: lock %s, cross-off %s, pause after failure %s", g_ok[0] ? "ready" : "UNAVAILABLE",
       g_ok[1] ? "ready" : "UNAVAILABLE", g_ok[2] ? "ready" : "UNAVAILABLE");
}

void tick(il2cpp::Object* game, unsigned requests) {
  requests |= g_injected;
  g_injected = 0;
  if (!g_bound) return;
  Object* booth = pp::booth_of(game);
  lock::tick(booth, switches::get(switches::kLockMaterials));
  criminals::tick(booth, switches::get(switches::kCrossOffCriminals));
  failhold::tick(game, booth, switches::get(switches::kPauseAfterFailure), (requests & panel::kRequestContinue) != 0);
}

void describe(panel::Model& m) {
  for (int i = 0; i < switches::kCount; ++i) m.feature_ok[i] = g_ok[i];
  if (!g_bound) return;
  m.lock_text = lock::describe();
  m.criminals_text = criminals::describe();
  m.holding = failhold::holding();
  m.hold_text = failhold::describe();
}

void inject_request(unsigned request) { g_injected |= request; }

void log_state() {
  Object* game = game::current_game();
  logf("state: game %p, game state %s, paused %d", static_cast<void*>(game),
       pp::str(pp::state_name(get<Object*>(game, F.game_stater))).c_str(), game::paused());
  Object* booth = pp::booth_of(game);
  if (!booth) {
    logf("state: no booth");
    return;
  }
  Object* day = get<Object*>(booth, F.booth_day);
  Object* engine = get<Object*>(booth, F.booth_engine);
  Object* env = get<Object*>(engine, F.engine_env);
  Object* traveler = get<Object*>(env, F.env_traveler);
  // Clock.get_time is sysCurTime - sysStartTime (its "time" field is unused).
  Object* clock = get<Object*>(booth, F.booth_clock);
  const double clock_time = get<double>(clock, F.clock_cur) - get<double>(clock, F.clock_start);
  logf("state: day %d (features %#x), booth state %s, booth clock %.2f, endless %p", get<int32_t>(day, F.day_id),
       get<int32_t>(day, F.day_features), pp::str(pp::state_name(get<Object*>(booth, F.booth_stater))).c_str(),
       clock_time, static_cast<void*>(get<Object*>(day, F.day_endless)));
  bool errors = false;
  if (traveler && pp::M.env_has_errors)
    errors = reinterpret_cast<bool (*)(Object*, Object*, const il2cpp::Method*)>(pp::M.env_has_errors->pointer)(
        env, nullptr, pp::M.env_has_errors);
  int32_t given = -1;
  if (pp::M.engine_given_approval) {
    Object* a = reinterpret_cast<Object* (*)(Object*, const il2cpp::Method*)>(pp::M.engine_given_approval->pointer)(
        engine, pp::M.engine_given_approval);
    if (a) given = get<int32_t>(a, F.approval_flags);
  }
  std::string confiscated;
  Object* ids = get<Object*>(engine, F.engine_confiscated);
  for (int i = 0; i < pp::array_length(ids); ++i) confiscated += (i ? "," : "") + pp::str(pp::array_get(ids, i));
  logf("state: engine mistakeCaught %d justMadeMistake %d given %d confiscated [%s]; traveler %p criminal %d "
       "detained %d, papers have errors %d",
       get<bool>(engine, F.engine_mistake), get<bool>(engine, F.engine_just_mistake), given, confiscated.c_str(),
       static_cast<void*>(traveler), traveler ? get<int32_t>(traveler, F.trav_criminal) : -1,
       traveler ? get<bool>(traveler, F.trav_detained) : 0, errors);
  Object* criminals = get<Object*>(day, F.day_criminals);
  for (int i = 0; i < pp::array_length(criminals); ++i) {
    Object* c = pp::array_get(criminals, i);
    logf("state: criminal %d: index %d used %d", i, get<int32_t>(c, F.crim_index), get<bool>(c, F.crim_used));
  }
  pp::list_each(get<Object*>(booth, F.booth_papers), [](Object* paper) {
    if (paper) logf("state: paper %s", paper_line(paper).c_str());
  });
}

}  // namespace ppcc::cheats
