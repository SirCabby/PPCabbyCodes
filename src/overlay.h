#pragma once

struct ImDrawData;

// The panel's renderer. Unity renders on a thread of its own (threaded
// rendering is on: "GfxDevice: creating device client; threaded=1"), so Dear
// ImGui lives entirely on that thread: each frame, as the game presents, it
// builds the panel from the model the game's thread published (panel.h) and
// draws it on the back buffer. How a present is caught and what draws is the
// platform's: glXSwapBuffers and OpenGL on Linux (linux/overlay_gl.cpp), the
// game's DXGI swap chain and Direct3D 11 on Windows (win/overlay_d3d11.cpp).
// This file's functions, and the panel's frame, are shared.
namespace ppcc::overlay {

// Hooks the game's presents; `dir` is where PPCabbyCodes.imgui.ini goes. Done
// from the library's start, before the game has a window, so nothing is
// presenting while the hooks go in.
bool install(const char* dir);

// What the game's thread saw of the mouse this frame (Unity's Input, read in
// the game's own frame). Coordinates are the window's pixels, top-left origin.
struct InputSample {
  float x = -1, y = -1;
  bool buttons[3] = {};  // left, right, middle: down, or pressed during the frame
  float wheel = 0;       // notches this frame
  int screen_w = 0, screen_h = 0;
};
void publish_input(const InputSample& s);

// True while the panel holds the mouse (it is over one of the panel's windows
// or dragging one): the game's pointer is then kept from the game.
bool wants_mouse();

// --- the renderers' side, on the render thread ----------------------------------

// The platform's present hooks (install calls it).
bool install_presents();

// ImGui's context, set up for the panel: its ini, the input it is fed.
void create_context();

// One panel frame for a back buffer of w x h pixels: the renderer's NewFrame,
// then the panel. The draw data to render, or null when there is nothing to draw.
ImDrawData* frame(unsigned w, unsigned h, void (*renderer_new_frame)());

}  // namespace ppcc::overlay
