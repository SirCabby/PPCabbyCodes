#pragma once

// Unity engine calls the mod makes from the game's thread: the input and
// screen state, through Unity's own internal calls (il2cpp_resolve_icall), so
// they work whether or not the game's code uses the managed wrappers.
namespace ppcc::unity {

struct Vector3 {
  float x, y, z;
};
struct Vector2 {
  float x, y;
};

bool bind();  // false (logged) when an internal call is missing

Vector3 mouse_position();  // pixels, bottom-left origin
bool mouse_button(int b);
bool mouse_button_down(int b);
Vector2 mouse_scroll();
bool key_down(int key_code);  // went down this frame
int screen_width();
int screen_height();

}  // namespace ppcc::unity
