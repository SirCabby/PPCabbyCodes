#include "pp.h"

#include "log.h"

namespace ppcc::pp {

Classes C;
Fields F;
Methods M;

namespace {

constexpr const char* kAsm = "Assembly-CSharp.dll";

il2cpp::Class* cls(const char* ns, const char* name) { return il2cpp::find_class(kAsm, ns, name); }

int off(il2cpp::Class* k, const char* field, int* missing) {
  const int o = k ? il2cpp::field_offset(k, field) : -1;
  if (o < 0) ++*missing;
  return o;
}

const Method* meth(il2cpp::Class* k, const char* name, int argc, int* missing) {
  const Method* m = k ? il2cpp::find_method(k, name, argc) : nullptr;
  if (!m) ++*missing;
  return m;
}

}  // namespace

bool bind() {
  int missing = 0;
  C.game = cls("", "Game");
  C.day_screen = cls("play.screen", "DayScreen");
  C.booth = cls("play.day.booth", "Booth");
  C.paper = cls("play.day.booth", "Paper");
  C.desk_item = cls("play.day.booth", "DeskItem");
  C.criminal_poster = cls("play.day.booth", "CriminalPoster");
  C.engine = cls("play.day", "BoothEngine");
  C.env = cls("play.day", "BoothEnv");
  C.day = cls("play.day", "Day");
  C.placement = cls("data", "DeskItemPlacement");
  C.desk_state = cls("data", "DeskItemState");
  C.engine_event = cls("play.day", "EngineEvent");
  C.endless = cls("play.day", "Endless");
  il2cpp::Class* paper_def = cls("data", "PaperDef");
  il2cpp::Class* ent = cls("app.ent", "Ent");
  il2cpp::Class* point = cls("app.vis", "PointData");
  il2cpp::Class* visual = cls("app.vis", "Visual");
  il2cpp::Class* criminal = cls("play.day", "Criminal");
  il2cpp::Class* traveler = cls("play.day", "Traveler");
  il2cpp::Class* array = cls("", "Array");
  il2cpp::Class* list = cls("haxe.ds", "List");
  il2cpp::Class* node = cls("haxe.ds._List", "ListNode");
  il2cpp::Class* henum = cls("haxe.lang", "Enum");
  il2cpp::Class* stater = cls("app", "Stater");
  il2cpp::Class* state = cls("app", "State");
  il2cpp::Class* clock = C.clock = cls("app", "Clock");
  il2cpp::Class* approval = cls("data", "StampApproval");

  F.game_screen = off(C.game, "gameScreen", &missing);
  F.game_stater = off(C.game, "stater", &missing);
  F.dayscreen_booth = off(C.day_screen, "booth", &missing);
  F.booth_papers = off(C.booth, "papers", &missing);
  F.booth_engine = off(C.booth, "engine", &missing);
  F.booth_day = off(C.booth, "day", &missing);
  F.booth_stater = off(C.booth, "stater", &missing);
  F.booth_clock = off(C.booth, "boothClock", &missing);
  F.booth_poster = off(C.booth, "criminalPoster", &missing);
  F.paper_def = off(C.paper, "def", &missing);
  F.paper_desk = off(C.paper, "deskItem", &missing);
  F.paper_page = off(C.paper, "pageIndex", &missing);
  F.paper_id = off(C.paper, "idWithIndex", &missing);
  F.def_id = off(paper_def, "id", &missing);
  F.def_stampable = off(paper_def, "stampable", &missing);
  F.def_from_traveler = off(paper_def, "fromTraveler", &missing);
  F.ent_pos = off(ent, "pos", &missing);
  F.point_x = off(point, "x", &missing);
  F.point_y = off(point, "y", &missing);
  F.desk_placement = off(C.desk_item, "placement", &missing);
  F.desk_state = off(C.desk_item, "state", &missing);
  F.desk_dragging = off(C.desk_item, "dragging", &missing);
  F.desk_visible = off(C.desk_item, "visible", &missing);
  F.day_id = off(C.day, "id", &missing);
  F.day_criminals = off(C.day, "criminals", &missing);
  F.day_endless = off(C.day, "endless", &missing);
  F.day_features = off(C.day, "featureFlags", &missing);
  F.crim_index = off(criminal, "index", &missing);
  F.crim_used = off(criminal, "used", &missing);
  F.engine_env = off(C.engine, "env", &missing);
  F.engine_mistake = off(C.engine, "mistakeCaught", &missing);
  F.engine_just_mistake = off(C.engine, "justMadeMistake", &missing);
  F.engine_confiscated = off(C.engine, "confiscatedPaperIds", &missing);
  F.env_traveler = off(C.env, "traveler", &missing);
  F.trav_criminal = off(traveler, "criminalIndex", &missing);
  F.trav_detained = off(traveler, "detained", &missing);
  F.trav_force_deny = off(traveler, "forceDeny", &missing);
  F.approval_flags = off(approval, "flags", &missing);
  F.endless_clock = off(C.endless, "clock", &missing);
  F.arr_length = off(array, "length", &missing);
  F.arr_items = off(array, "__a", &missing);
  F.list_head = off(list, "h", &missing);
  F.node_item = off(node, "item", &missing);
  F.node_next = off(node, "next", &missing);
  F.enum_index = off(henum, "_hx_index", &missing);
  F.stater_cur = off(stater, "curState", &missing);
  F.state_name = off(state, "name", &missing);
  F.clock_cur = off(clock, "sysCurTime", &missing);
  F.clock_start = off(clock, "sysStartTime", &missing);
  F.visual_visible = off(visual, "visible", &missing);
  F.poster_sprites = off(C.criminal_poster, "sprites", &missing);

  M.paper_set_page = meth(C.paper, "setPage", 1, &missing);
  M.paper_page_id = meth(C.paper, "get_pageId", 0, &missing);
  M.def_find_page = meth(paper_def, "findPageIndex", 1, &missing);
  M.desk_set_placement = meth(C.desk_item, "set_placement", 1, &missing);
  M.desk_clip_rects = meth(C.desk_item, "updateClipRects", 0, &missing);
  M.paper_update_page = meth(C.paper, "updatePage", 0, &missing);
  M.env_fact_text = meth(C.env, "getFactValueText", 2, &missing);
  M.desk_update_drag = meth(C.desk_item, "updateCustomDrag", 1, &missing);
  M.desk_return_dock = meth(C.desk_item, "returnToDock", 0, &missing);
  M.env_has_errors = meth(C.env, "hasErrors", 1, &missing);
  M.engine_given_approval = meth(C.engine, "get_givenApproval", 0, &missing);
  M.engine_paper_approval = meth(C.engine, "getPaperApproval", 1, &missing);
  M.engine_set_given = meth(C.engine, "set_givenApproval", 1, &missing);
  M.approval_combine = meth(approval, "combine", 2, &missing);
  M.env_want_confiscate = meth(C.env, "wantConfiscatePassport", 0, &missing);
  M.env_had_paper = meth(C.env, "hadPaper", 1, &missing);
  M.clock_pause = meth(clock, "pauseForOneFrame", 0, &missing);
  M.booth_on_given = meth(C.booth, "deskItem_onGiven", 1, &missing);
  M.engine_handle_event = meth(C.engine, "handleEvent", 1, &missing);
  M.endless_handle_action = meth(C.endless, "handleAction", 1, &missing);
  M.clock_update = meth(clock, "update", 1, &missing);
  M.env_get_image = meth(C.env, "getImage", 5, &missing);

  if (missing) logf("pp: %d of the game's fields/methods were not found", missing);
  else logf("pp: the game's classes, fields and methods are all there");
  return missing == 0;
}

Object* placement_value(int index) {
  static const char* kNames[] = {"OUTER", "INNER", "FILER", "MOUNT"};
  if (!C.placement || index < 0 || index > 3) return nullptr;
  il2cpp::il2cpp_runtime_class_init(C.placement);
  il2cpp::Field* f = il2cpp::il2cpp_class_get_field_from_name(C.placement, kNames[index]);
  if (!f) return nullptr;
  Object* v = nullptr;
  il2cpp::il2cpp_field_static_get_value(f, &v);
  return v;
}

Object* static_object(il2cpp::Class* klass, const char* field) {
  if (!klass) return nullptr;
  il2cpp::il2cpp_runtime_class_init(klass);
  il2cpp::Field* f = il2cpp::il2cpp_class_get_field_from_name(klass, field);
  if (!f) {
    logf("pp: no static %s.%s", il2cpp::il2cpp_class_get_name(klass), field);
    return nullptr;
  }
  Object* v = nullptr;
  il2cpp::il2cpp_field_static_get_value(f, &v);
  return v;
}

int array_length(Object* a) { return a ? il2cpp::get<int32_t>(a, F.arr_length) : 0; }

Object* array_get(Object* a, int i) {
  if (!a || i < 0 || i >= array_length(a)) return nullptr;
  auto* items = il2cpp::get<il2cpp::Array*>(a, F.arr_items);
  return il2cpp::array_at(items, static_cast<size_t>(i));
}

int enum_index(Object* e) { return e ? il2cpp::get<int32_t>(e, F.enum_index) : -1; }

bool str_eq(Object* o, const char* ascii) {
  auto* s = reinterpret_cast<il2cpp::String*>(o);
  if (!s || !ascii) return false;
  int32_t i = 0;
  for (; ascii[i]; ++i)
    if (i >= s->length || s->chars[i] != static_cast<char16_t>(static_cast<unsigned char>(ascii[i]))) return false;
  return i == s->length;
}

std::string str(Object* s) { return il2cpp::to_utf8(reinterpret_cast<il2cpp::String*>(s)); }

il2cpp::String* new_string(const std::string& utf8) { return il2cpp::il2cpp_string_new(utf8.c_str()); }

Object* booth_of(Object* game) {
  Object* screen = il2cpp::get<Object*>(game, F.game_screen);
  if (!screen || !il2cpp::is_instance(screen, C.day_screen)) return nullptr;
  return il2cpp::get<Object*>(screen, F.dayscreen_booth);
}

Object* state_name(Object* stater) {
  Object* st = il2cpp::get<Object*>(stater, F.stater_cur);
  return il2cpp::get<Object*>(st, F.state_name);
}

Object* paper_def(Object* paper) { return il2cpp::get<Object*>(paper, F.paper_def); }
Object* paper_desk(Object* paper) { return il2cpp::get<Object*>(paper, F.paper_desk); }

Object* find_paper(Object* booth, const char* def_id) {
  Object* found = nullptr;
  list_each(il2cpp::get<Object*>(booth, F.booth_papers), [&](Object* p) {
    if (!found && p && str_eq(il2cpp::get<Object*>(paper_def(p), F.def_id), def_id)) found = p;
  });
  return found;
}

Pos ent_pos(Object* ent) {
  Object* p = il2cpp::get<Object*>(ent, F.ent_pos);
  return Pos{il2cpp::get<double>(p, F.point_x), il2cpp::get<double>(p, F.point_y)};
}

void set_ent_pos(Object* ent, Pos pos) {
  Object* p = il2cpp::get<Object*>(ent, F.ent_pos);
  il2cpp::set<double>(p, F.point_x, pos.x);
  il2cpp::set<double>(p, F.point_y, pos.y);
}

}  // namespace ppcc::pp
