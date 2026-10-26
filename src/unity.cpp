#include "unity.h"

#include "il2cpp.h"
#include "log.h"

namespace ppcc::unity {
namespace {

void (*g_mouse_position)(Vector3*) = nullptr;
bool (*g_mouse_button)(int) = nullptr;
bool (*g_mouse_button_down)(int) = nullptr;
void (*g_mouse_scroll)(Vector2*) = nullptr;
bool (*g_key_down)(int) = nullptr;
int (*g_screen_width)() = nullptr;
int (*g_screen_height)() = nullptr;

template <typename T>
bool resolve(T& fn, const char* name) {
  fn = reinterpret_cast<T>(il2cpp::il2cpp_resolve_icall(name));
  if (!fn) logf("unity: no internal call %s", name);
  return fn != nullptr;
}

}  // namespace

bool bind() {
  bool ok = true;
  ok &= resolve(g_mouse_position, "UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)");
  ok &= resolve(g_mouse_button, "UnityEngine.Input::GetMouseButton(System.Int32)");
  ok &= resolve(g_mouse_button_down, "UnityEngine.Input::GetMouseButtonDown(System.Int32)");
  ok &= resolve(g_mouse_scroll, "UnityEngine.Input::get_mouseScrollDelta_Injected(UnityEngine.Vector2&)");
  ok &= resolve(g_key_down, "UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)");
  ok &= resolve(g_screen_width, "UnityEngine.Screen::get_width()");
  ok &= resolve(g_screen_height, "UnityEngine.Screen::get_height()");
  return ok;
}

Vector3 mouse_position() {
  Vector3 v{-1, -1, 0};
  if (g_mouse_position) g_mouse_position(&v);
  return v;
}
bool mouse_button(int b) { return g_mouse_button && g_mouse_button(b); }
bool mouse_button_down(int b) { return g_mouse_button_down && g_mouse_button_down(b); }
Vector2 mouse_scroll() {
  Vector2 v{0, 0};
  if (g_mouse_scroll) g_mouse_scroll(&v);
  return v;
}
bool key_down(int key_code) { return g_key_down && key_code >= 0 && g_key_down(key_code); }
int screen_width() { return g_screen_width ? g_screen_width() : 0; }
int screen_height() { return g_screen_height ? g_screen_height() : 0; }

}  // namespace ppcc::unity
