#include "failhold.h"

#include <string>
#include <vector>

#include "hook.h"
#include "log.h"
#include "pp.h"

namespace ppcc::failhold {
namespace {

using il2cpp::get;
using il2cpp::Method;
using pp::F;
using pp::M;
using pp::Object;

constexpr int32_t kApproved = 2, kDenied = 4, kReasoned = 8;  // StampApproval.flags

using OnGivenFn = void (*)(Object*, Object*, const Method*);
using HandleEventFn = void (*)(Object*, Object*, const Method*);
using HandleActionFn = void (*)(Object*, Object*, const Method*);
using ClockUpdateFn = void (*)(Object*, Object*, const Method*);
OnGivenFn g_on_given = nullptr;
HandleEventFn g_handle_event = nullptr;
HandleActionFn g_handle_action = nullptr;
ClockUpdateFn g_clock_update = nullptr;

Object* g_processing_finish = nullptr;  // EngineEvent.PROCESSING_FINISH
Object* g_state_normal = nullptr;       // DeskItemState.NORMAL
Object* g_state_giving = nullptr;       // DeskItemState.GIVING

bool g_ready = false;
bool g_on = false;

struct Hold {
  bool active = false;
  Object* booth = nullptr;
  Object* traveler = nullptr;
  std::vector<Object*> kept;  // documents handed back during the hold, back on the counter
  Object* clocks[2] = {};     // the booth's clock and an endless run's
};
Hold g_hold;
// The traveler a hold was continued for (no second hold), and the one whose
// judgement already ran at the hold (the game's own run of it is skipped). Both
// are let go as soon as the booth has another traveler or the booth goes: a
// traveler that has gone is freed, and a new one can be given its address.
Object* g_continued = nullptr;
Object* g_judged = nullptr;
bool g_judging = false;            // inside that early judgement
Object* g_deferred_endless = nullptr;
std::string g_deferred_action;     // the endless run's action, kept back until Continue
Object* g_booth = nullptr;         // the booth, and its traveler, as the last tick saw them
Object* g_traveler = nullptr;
// Why the current traveler has not been held, for the log should the game cite
// them at their departure.
const char* g_why_not = "";

template <typename R, typename... A>
R call(const Method* m, A... args) {
  return reinterpret_cast<R (*)(A..., const Method*)>(m->pointer)(args..., m);
}

int32_t approval_flags(Object* approval) { return approval ? get<int32_t>(approval, F.approval_flags) : 0; }

bool array_has(Object* array, const char* ascii) {
  for (int i = 0; i < pp::array_length(array); ++i)
    if (pp::str_eq(pp::array_get(array, i), ascii)) return true;
  return false;
}

Object* traveler_of(Object* booth) {
  return get<Object*>(get<Object*>(get<Object*>(booth, F.booth_engine), F.engine_env), F.env_traveler);
}

int desk_state(Object* desk) { return pp::enum_index(get<Object*>(desk, F.desk_state)); }
bool in_filer(Object* desk) { return pp::enum_index(get<Object*>(desk, F.desk_placement)) == pp::kFiler; }

// Whether a stampable paper other than `handing` is still on the desk. Until
// each is handed back or put in the filer, the judgement is not settled: a
// passport may yet be confiscated, or stamped again, and one left on the desk is
// confiscated by the game at the departure (Booth.hideTransientPapers) without
// its stamps counting. A confiscated passport's verdict travels on the seizure
// slip the booth issues for it, a stampable paper of the inspector's (not
// fromTraveler), so every stampable paper counts here.
bool verdict_open(Object* booth, Object* handing) {
  bool open = false;
  pp::list_each(get<Object*>(booth, F.booth_papers), [&](Object* paper) {
    Object* def = pp::paper_def(paper);
    if (open || !def || !get<bool>(def, F.def_stampable)) return;
    Object* desk = pp::paper_desk(paper);
    if (desk == handing || in_filer(desk)) return;
    const int state = desk_state(desk);
    if (state == pp::kGiving || state == pp::kGiven || state == pp::kHiding || state == pp::kHidden) return;
    open = true;
  });
  return open;
}

// The stamped papers on their way to the traveler that the engine has not had
// yet: the one handed back now (already GIVEN when the booth hears of it) and
// any still in the air. Only papers given to the traveler count in
// givenApproval (BoothEngine.givePaperToTraveler): not one in the filer.
template <typename Fn>
void for_on_the_way(Object* booth, Object* handing, Fn fn) {
  pp::list_each(get<Object*>(booth, F.booth_papers), [&](Object* paper) {
    Object* def = pp::paper_def(paper);
    if (!def || !get<bool>(def, F.def_stampable)) return;
    Object* desk = pp::paper_desk(paper);
    if (desk != handing && desk_state(desk) != pp::kGiving) return;
    fn(paper);
  });
}

// What the engine's judgement (BoothEngine.handleEvent, PROCESSING_FINISH) will
// say once the traveler has everything back and has left, judged as `handing`
// is handed back with the verdict settled (verdict_open is false): the stamps
// given, the confiscation the day's rules ask for, and the denial reason.
bool mistake_pending(Object* booth, Object* handing) {
  Object* engine = get<Object*>(booth, F.booth_engine);
  Object* env = get<Object*>(engine, F.engine_env);
  Object* traveler = get<Object*>(env, F.env_traveler);
  Object* day = get<Object*>(booth, F.booth_day);
  if (!engine || !env || !traveler || !day) return false;
  const unsigned features = static_cast<unsigned>(get<int32_t>(day, F.day_features));
  int32_t flags = approval_flags(call<Object*>(M.engine_given_approval, engine));
  for_on_the_way(booth, handing, [&](Object* paper) {
    flags |= approval_flags(call<Object*>(M.engine_paper_approval, engine, get<Object*>(paper, F.paper_id)));
  });
  bool mistake = false;
  if (!get<bool>(traveler, F.trav_force_deny)) {
    if (features & pp::kFeatureFiler) {
      const bool want = call<bool>(M.env_want_confiscate, env);
      const bool had = call<bool>(M.env_had_paper, env, pp::new_string("Passport"));
      const bool confiscated = array_has(get<Object*>(engine, F.engine_confiscated), "Passport");
      if (had && want != confiscated) mistake = true;
    }
    if (get<bool>(engine, F.engine_mistake) && !get<bool>(traveler, F.trav_detained)) {
      const bool approved = (flags & kApproved) != 0;
      const bool errors = call<bool>(M.env_has_errors, env, static_cast<Object*>(nullptr));
      if (approved == errors) mistake = true;
    }
  }
  if ((features & pp::kFeatureDenialReason) && (flags & kDenied) && !(flags & kReasoned)) mistake = true;
  return mistake;
}

// Whether the desk item is one of the booth's papers.
bool is_paper(Object* booth, Object* desk) {
  bool found = false;
  pp::list_each(get<Object*>(booth, F.booth_papers), [&](Object* paper) {
    if (paper && pp::paper_desk(paper) == desk) found = true;
  });
  return found;
}

// A document handed back during the hold stays: back on the counter (the
// game's own fall puts it down), visible, and the engine never hears of it.
void keep(Object* desk) {
  il2cpp::set<Object*>(desk, F.desk_state, g_state_normal);
  il2cpp::set<bool>(desk, F.desk_visible, true);
  g_hold.kept.push_back(desk);
}

// The approvals the traveler leaves with: what was given so far, and the
// stamped paper being handed back (as BoothEngine.givePaperToTraveler adds
// it). The judgement reads givenApproval, so it has them before it runs early.
void give_approvals_early(Object* booth, Object* engine, Object* handing) {
  for_on_the_way(booth, handing, [&](Object* paper) {
    Object* own = call<Object*>(M.engine_paper_approval, engine, get<Object*>(paper, F.paper_id));
    Object* given = call<Object*>(M.engine_given_approval, engine);
    Object* both = reinterpret_cast<Object* (*)(Object*, Object*, const Method*)>(M.approval_combine->pointer)(
        own, given, M.approval_combine);
    call<void>(M.engine_set_given, engine, both);
  });
}

void finish();

void begin(Object* booth, Object* desk) {
  Object* engine = get<Object*>(booth, F.booth_engine);
  Object* env = get<Object*>(engine, F.engine_env);
  Object* day = get<Object*>(booth, F.booth_day);
  g_hold = Hold{};
  g_hold.active = true;
  g_hold.booth = booth;
  g_hold.traveler = get<Object*>(env, F.env_traveler);
  g_hold.clocks[0] = get<Object*>(booth, F.booth_clock);
  g_hold.clocks[1] = get<Object*>(get<Object*>(day, F.day_endless), F.endless_clock);
  keep(desk);
  // The judgement now, so its citation prints while the papers are still here.
  give_approvals_early(booth, engine, desk);
  g_deferred_endless = nullptr;
  g_deferred_action.clear();
  g_judging = true;
  g_handle_event(engine, g_processing_finish, M.engine_handle_event);
  g_judging = false;
  g_judged = g_hold.traveler;
  if (!get<bool>(engine, F.engine_just_mistake)) {
    // The game sees no mistake after all: nothing to hold for.
    logf("failhold: the judgement found no mistake - not holding");
    finish();
    return;
  }
  logf("failhold: a mistake - holding (traveler %p)%s", static_cast<void*>(g_hold.traveler),
       g_deferred_action.empty() ? "" : (", endless action \"" + g_deferred_action + "\" kept back").c_str());
}

void finish() {
  g_hold.active = false;
  if (!g_hold.traveler) return;
  g_continued = g_hold.traveler;
  // The documents go to the traveler after all: the game's own giving (its
  // update finishes it and tells the booth, as for any document).
  for (Object* desk : g_hold.kept) il2cpp::set<Object*>(desk, F.desk_state, g_state_giving);
  if (g_deferred_endless) {
    logf("failhold: continue - endless action \"%s\"", g_deferred_action.c_str());
    g_handle_action(g_deferred_endless, pp::new_string(g_deferred_action), M.endless_handle_action);
  } else {
    logf("failhold: continue");
  }
  g_deferred_endless = nullptr;
  g_deferred_action.clear();
  g_hold = Hold{};
}

void hooked_on_given(Object* booth, Object* desk, const Method* method) {
  if (g_hold.active && booth == g_hold.booth) {
    keep(desk);
    return;
  }
  if (booth && desk && is_paper(booth, desk)) {
    Object* traveler = traveler_of(booth);
    if (!g_on) {
      g_why_not = "the switch was off at the last hand-back";
    } else if (traveler && traveler != g_continued) {
      if (verdict_open(booth, desk)) {
        g_why_not = "a stamped paper of theirs was still on the desk at the last hand-back";
      } else if (mistake_pending(booth, desk)) {
        begin(booth, desk);
        return;
      } else {
        g_why_not = "no mistake was foreseen at the last hand-back";
      }
    }
  }
  g_on_given(booth, desk, method);
}

void hooked_handle_event(Object* engine, Object* event, const Method* method) {
  if (!event || event != g_processing_finish) {
    g_handle_event(engine, event, method);
    return;
  }
  Object* traveler = get<Object*>(get<Object*>(engine, F.engine_env), F.env_traveler);
  if (g_judged && traveler == g_judged) {
    g_judged = nullptr;
    logf("failhold: the traveler's judgement already ran at the hold - skipped");
    return;
  }
  g_handle_event(engine, event, method);
  // A citation the hold did not catch: why, for a report.
  if (g_on && get<bool>(engine, F.engine_just_mistake))
    logf("failhold: the game cited a mistake at the departure that was not held - %s%s", g_why_not,
         traveler && get<bool>(traveler, F.trav_detained) ? " (the traveler was detained)" : "");
}

void hooked_handle_action(Object* endless, Object* action, const Method* method) {
  if (g_judging) {
    g_deferred_endless = endless;
    g_deferred_action = pp::str(action);
    return;
  }
  g_handle_action(endless, action, method);
}

void hooked_clock_update(Object* clock, Object* force, const Method* method) {
  g_clock_update(clock, force, method);
  if (g_hold.active && clock && (clock == g_hold.clocks[0] || clock == g_hold.clocks[1]))
    call<void>(M.clock_pause, clock);  // what this update added is taken back: the clock stands still
}

}  // namespace

bool start() {
  if (!M.booth_on_given || !M.engine_handle_event || !M.endless_handle_action || !M.clock_update ||
      !M.clock_pause || !M.engine_given_approval || !M.engine_paper_approval || !M.env_has_errors ||
      !M.engine_set_given || !M.approval_combine || !M.env_want_confiscate || !M.env_had_paper)
    return false;
  g_processing_finish = pp::static_object(pp::C.engine_event, "PROCESSING_FINISH");
  g_state_normal = pp::static_object(pp::C.desk_state, "NORMAL");
  g_state_giving = pp::static_object(pp::C.desk_state, "GIVING");
  if (!g_processing_finish || !g_state_normal || !g_state_giving) return false;
  g_ready = hook::inline_hook(M.booth_on_given->pointer, reinterpret_cast<void*>(&hooked_on_given),
                              reinterpret_cast<void**>(&g_on_given), "Booth.deskItem_onGiven") &&
            hook::inline_hook(M.engine_handle_event->pointer, reinterpret_cast<void*>(&hooked_handle_event),
                              reinterpret_cast<void**>(&g_handle_event), "BoothEngine.handleEvent") &&
            hook::inline_hook(M.endless_handle_action->pointer, reinterpret_cast<void*>(&hooked_handle_action),
                              reinterpret_cast<void**>(&g_handle_action), "Endless.handleAction") &&
            hook::inline_hook(M.clock_update->pointer, reinterpret_cast<void*>(&hooked_clock_update),
                              reinterpret_cast<void**>(&g_clock_update), "Clock.update");
  return g_ready;
}

void tick(Object*, Object* booth, bool on, bool continue_requested) {
  g_on = on;
  if (!g_ready) return;
  // Another booth (a day or run left, a new one begun): nothing of the old one's
  // travelers carries over.
  if (booth != g_booth) {
    g_booth = booth;
    g_continued = nullptr;
    g_judged = nullptr;
  }
  Object* traveler = booth ? traveler_of(booth) : nullptr;
  if (traveler != g_traveler) {
    g_traveler = traveler;
    g_why_not = "nothing of theirs was handed back";
    if (g_continued != traveler) g_continued = nullptr;
    if (g_judged != traveler) g_judged = nullptr;
  }
  if (!g_hold.active) return;
  if (booth != g_hold.booth || traveler != g_hold.traveler) {
    // The day was left during the hold, or the traveler went (detained): nothing
    // to hand over. An endless run still takes the mistake its judgement made.
    logf("failhold: the %s went away during the hold", booth != g_hold.booth ? "booth" : "traveler");
    if (booth == g_hold.booth && g_deferred_endless)
      g_handle_action(g_deferred_endless, pp::new_string(g_deferred_action), M.endless_handle_action);
    g_hold = Hold{};
    g_judged = nullptr;
    g_deferred_endless = nullptr;
    g_deferred_action.clear();
    return;
  }
  if (continue_requested || !on) finish();
}

bool holding() { return g_hold.active; }

std::string describe() { return g_hold.active ? "Mistake - the papers are held" : ""; }

}  // namespace ppcc::failhold
