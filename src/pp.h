#pragma once

#include <cstdint>
#include <string>

#include "il2cpp.h"

// Papers, Please's object model, as far as the mod uses it. The game is Haxe
// compiled to C# (then IL2CPP): its classes keep their Haxe names and
// namespaces (play.day.booth.Booth), Haxe arrays are a class of their own
// (Array: length + object[]), lists are linked (haxe.ds.List), enums are
// objects with an index (haxe.lang.Enum._hx_index), and nearly every method is
// virtual. pp::bind() looks every class, field and method up by name once; the
// helpers here read the game's objects with those offsets.
namespace ppcc::pp {

using il2cpp::Method;
using il2cpp::Object;

struct Classes {
  il2cpp::Class* game = nullptr;
  il2cpp::Class* day_screen = nullptr;
  il2cpp::Class* booth = nullptr;
  il2cpp::Class* paper = nullptr;
  il2cpp::Class* desk_item = nullptr;
  il2cpp::Class* criminal_poster = nullptr;
  il2cpp::Class* engine = nullptr;
  il2cpp::Class* env = nullptr;
  il2cpp::Class* day = nullptr;
  il2cpp::Class* placement = nullptr;  // data.DeskItemPlacement
  il2cpp::Class* desk_state = nullptr; // data.DeskItemState
  il2cpp::Class* engine_event = nullptr;  // play.day.EngineEvent
  il2cpp::Class* endless = nullptr;
  il2cpp::Class* clock = nullptr;
};

// Field offsets (-1: not found; reads give 0/null, writes do nothing).
struct Fields {
  int game_screen = -1, game_stater = -1;
  int dayscreen_booth = -1;
  int booth_papers = -1, booth_engine = -1, booth_day = -1, booth_stater = -1, booth_clock = -1, booth_poster = -1;
  int paper_def = -1, paper_desk = -1, paper_page = -1, paper_id = -1;
  int def_id = -1, def_stampable = -1, def_from_traveler = -1;
  int ent_pos = -1, point_x = -1, point_y = -1;
  int desk_placement = -1, desk_state = -1, desk_dragging = -1, desk_visible = -1;
  int day_id = -1, day_criminals = -1, day_endless = -1, day_features = -1;
  int crim_index = -1, crim_used = -1;
  int engine_env = -1, engine_mistake = -1, engine_just_mistake = -1, engine_confiscated = -1;
  int env_traveler = -1;
  int trav_criminal = -1, trav_detained = -1, trav_force_deny = -1;
  int approval_flags = -1;  // data.StampApproval.flags: 1 << StampApprovalKind (2 approved, 4 denied, 8 reasoned)
  int endless_clock = -1;
  int arr_length = -1, arr_items = -1, list_head = -1, node_item = -1, node_next = -1, enum_index = -1;
  int stater_cur = -1, state_name = -1;
  int clock_cur = -1, clock_start = -1;  // Clock.get_time() = sysCurTime - sysStartTime
  int visual_visible = -1;
  int poster_sprites = -1;
};

struct Methods {
  const Method* paper_set_page = nullptr;     // Paper.setPage(int)
  const Method* paper_page_id = nullptr;      // Paper.get_pageId()
  const Method* def_find_page = nullptr;      // PaperDef.findPageIndex(string)
  const Method* desk_set_placement = nullptr; // DeskItem.set_placement(DeskItemPlacement)
  const Method* desk_clip_rects = nullptr;    // DeskItem.updateClipRects()
  const Method* paper_update_page = nullptr;  // Paper.updatePage(): rebuilds the page's visuals
  const Method* env_fact_text = nullptr;      // BoothEnv.getFactValueText(path, default)
  const Method* desk_update_drag = nullptr;   // DeskItem.updateCustomDrag(dt) - hooked
  const Method* desk_return_dock = nullptr;   // DeskItem.returnToDock() - hooked
  const Method* env_has_errors = nullptr;     // BoothEnv.hasErrors(debugIncludingUnnoticable)
  const Method* engine_given_approval = nullptr;  // BoothEngine.get_givenApproval()
  const Method* engine_paper_approval = nullptr;  // BoothEngine.getPaperApproval(idWithIndex)
  const Method* engine_set_given = nullptr;   // BoothEngine.set_givenApproval(StampApproval)
  const Method* approval_combine = nullptr;   // StampApproval.combine(a, b) (static)
  const Method* env_want_confiscate = nullptr;  // BoothEnv.wantConfiscatePassport()
  const Method* env_had_paper = nullptr;      // BoothEnv.hadPaper(paperId)
  const Method* clock_pause = nullptr;        // Clock.pauseForOneFrame()
  const Method* booth_on_given = nullptr;     // Booth.deskItem_onGiven(DeskItem) - hooked
  const Method* engine_handle_event = nullptr;  // BoothEngine.handleEvent(EngineEvent) - hooked
  const Method* endless_handle_action = nullptr;  // Endless.handleAction(actionId) - hooked
  const Method* clock_update = nullptr;       // Clock.update(forceDeltaTime) - hooked
  const Method* env_get_image = nullptr;      // BoothEnv.getImage(...) - hooked
};

extern Classes C;
extern Fields F;
extern Methods M;

// Looks everything up; false (with the misses in the log) when something the
// mod's core needs is missing. Each switch checks its own pieces on top.
bool bind();

// Day.featureFlags: bit = play.day.Feature's index.
enum FeatureBit : unsigned { kFeatureFiler = 1u << 5, kFeatureCriminals = 1u << 8, kFeatureDenialReason = 1u << 9 };

// DeskItemPlacement / DeskItemState as their indices.
enum Placement { kOuter = 0, kInner = 1, kFiler = 2, kMount = 3 };
enum DeskState { kNone = 0, kRevealing = 1, kNormal = 2, kReturningToDock = 3, kHiding = 4, kHidden = 5,
                 kGiving = 6, kGiven = 7 };
Object* placement_value(int index);  // the enum object (statics of DeskItemPlacement)

// A static field holding an object (a Haxe enum's constant: DeskItemState.NORMAL...).
Object* static_object(il2cpp::Class* klass, const char* field);

// --- Haxe containers, enums, strings ---------------------------------------------
int array_length(Object* a);
Object* array_get(Object* a, int i);
template <typename Fn>
void list_each(Object* list, Fn fn) {
  Object* node = il2cpp::get<Object*>(list, F.list_head);
  for (int guard = 0; node && guard < 10000; ++guard) {
    fn(il2cpp::get<Object*>(node, F.node_item));
    node = il2cpp::get<Object*>(node, F.node_next);
  }
}
int enum_index(Object* e);
bool str_eq(Object* s, const char* ascii);
std::string str(Object* s);

// A managed string (new each call: a native pointer does not keep it alive).
il2cpp::String* new_string(const std::string& utf8);

// --- the game's objects -------------------------------------------------------------
Object* booth_of(Object* game);       // the day's booth, when a day is on screen
Object* state_name(Object* stater);   // the managed name of the current state
Object* paper_def(Object* paper);
Object* paper_desk(Object* paper);
Object* find_paper(Object* booth, const char* def_id);  // the first paper with this PaperDef id

struct Pos {
  double x = 0, y = 0;
};
Pos ent_pos(Object* ent);
void set_ent_pos(Object* ent, Pos p);

}  // namespace ppcc::pp
