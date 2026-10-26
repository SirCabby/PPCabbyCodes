// The panel's renderer on Linux: glXSwapBuffers, hooked, draws the panel with
// ImGui's OpenGL 3 backend into the back buffer about to be shown. Unity's
// OpenGL context is current on its render thread, which calls it.
#include <dlfcn.h>

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "hook.h"
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "log.h"
#include "overlay.h"
#include "testkit.h"

namespace ppcc::overlay {
namespace {

// GLX and GL, declared here so the build needs no X11/GL headers.
using Display = void;
using GLXDrawable = unsigned long;
constexpr int kGlxWidth = 0x801D, kGlxHeight = 0x801E;
using GLenum = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLboolean = unsigned char;
constexpr GLenum kDrawFramebuffer = 0x8CA9, kDrawFramebufferBinding = 0x8CA6, kFramebufferSrgb = 0x8DB9,
                 kColorWritemask = 0x0C23, kUnpackRowLength = 0x0CF2, kUnpackSkipRows = 0x0CF3,
                 kUnpackSkipPixels = 0x0CF4, kUnpackAlignment = 0x0CF5, kRasterizerDiscard = 0x8C89,
                 kMajorVersion = 0x821B, kMinorVersion = 0x821C;

using SwapFn = void (*)(Display*, GLXDrawable);
using QueryDrawableFn = void (*)(Display*, GLXDrawable, int, unsigned int*);
using GetCurrentContextFn = void* (*)();
using GetProcFn = void* (*)(const unsigned char*);

SwapFn g_real_swap = nullptr;
QueryDrawableFn g_query_drawable = nullptr;
GetCurrentContextFn g_current_context = nullptr;
GetProcFn g_get_proc = nullptr;

struct Gl {
  void (*GetIntegerv)(GLenum, GLint*) = nullptr;
  void (*GetBooleanv)(GLenum, GLboolean*) = nullptr;
  GLboolean (*IsEnabled)(GLenum) = nullptr;
  void (*Enable)(GLenum) = nullptr;
  void (*Disable)(GLenum) = nullptr;
  void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
  void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean) = nullptr;
  void (*PixelStorei)(GLenum, GLint) = nullptr;
  void (*ReadPixels)(GLint, GLint, int, int, GLenum, GLenum, void*) = nullptr;
  void (*ReadBuffer)(GLenum) = nullptr;
  bool ok() const { return GetIntegerv && GetBooleanv && IsEnabled && Enable && Disable && BindFramebuffer && ColorMask && PixelStorei; }
} gl;

bool g_ready = false;   // ImGui and its GL backend are up
bool g_failed = false;  // they could not be: the hook only presents

template <typename T>
void load(T& fn, const char* name) {
  fn = reinterpret_cast<T>(g_get_proc(reinterpret_cast<const unsigned char*>(name)));
}

// What ImGui's GL backend changes and does not put back, saved around it.
struct StateGuard {
  GLint draw_fb = 0, row_length = 0, skip_rows = 0, skip_pixels = 0, alignment = 4;
  GLboolean srgb = 0, discard = 0, mask[4] = {1, 1, 1, 1};
  StateGuard() {
    gl.GetIntegerv(kDrawFramebufferBinding, &draw_fb);
    gl.GetIntegerv(kUnpackRowLength, &row_length);
    gl.GetIntegerv(kUnpackSkipRows, &skip_rows);
    gl.GetIntegerv(kUnpackSkipPixels, &skip_pixels);
    gl.GetIntegerv(kUnpackAlignment, &alignment);
    gl.GetBooleanv(kColorWritemask, mask);
    srgb = gl.IsEnabled(kFramebufferSrgb);
    discard = gl.IsEnabled(kRasterizerDiscard);
    gl.BindFramebuffer(kDrawFramebuffer, 0);
    if (srgb) gl.Disable(kFramebufferSrgb);
    if (discard) gl.Disable(kRasterizerDiscard);
    gl.ColorMask(1, 1, 1, 1);
    gl.PixelStorei(kUnpackSkipRows, 0);
    gl.PixelStorei(kUnpackSkipPixels, 0);
  }
  ~StateGuard() {
    gl.PixelStorei(kUnpackRowLength, row_length);
    gl.PixelStorei(kUnpackSkipRows, skip_rows);
    gl.PixelStorei(kUnpackSkipPixels, skip_pixels);
    gl.PixelStorei(kUnpackAlignment, alignment);
    gl.ColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (discard) gl.Enable(kRasterizerDiscard);
    if (srgb) gl.Enable(kFramebufferSrgb);
    gl.BindFramebuffer(kDrawFramebuffer, static_cast<GLuint>(draw_fb));
  }
};

bool init_imgui() {
  load(gl.GetIntegerv, "glGetIntegerv");
  load(gl.GetBooleanv, "glGetBooleanv");
  load(gl.IsEnabled, "glIsEnabled");
  load(gl.Enable, "glEnable");
  load(gl.Disable, "glDisable");
  load(gl.BindFramebuffer, "glBindFramebuffer");
  load(gl.ColorMask, "glColorMask");
  load(gl.PixelStorei, "glPixelStorei");
  load(gl.ReadPixels, "glReadPixels");
  load(gl.ReadBuffer, "glReadBuffer");
  if (!gl.ok()) {
    logf("overlay: OpenGL functions missing - no panel");
    return false;
  }
  GLint major = 0, minor = 0;
  gl.GetIntegerv(kMajorVersion, &major);
  gl.GetIntegerv(kMinorVersion, &minor);
  create_context();
  // GLSL 1.50 for a 3.2+ context (Unity's is 4.5 core, where 1.30 is not
  // guaranteed); the backend's default otherwise.
  const bool core = major > 3 || (major == 3 && minor >= 2);
  if (!ImGui_ImplOpenGL3_Init(core ? "#version 150" : nullptr)) {
    logf("overlay: ImGui's OpenGL backend failed to start (GL %d.%d) - no panel", major, minor);
    ImGui::DestroyContext();
    return false;
  }
  logf("overlay: ImGui %s up on OpenGL %d.%d", IMGUI_VERSION, major, minor);
  return true;
}

void render(Display* dpy, GLXDrawable drawable) {
  if (!g_current_context || !g_current_context()) return;
  unsigned w = 0, h = 0;
  g_query_drawable(dpy, drawable, kGlxWidth, &w);
  g_query_drawable(dpy, drawable, kGlxHeight, &h);
  if (w == 0 || h == 0) return;
  if (!g_ready) {
    if (!init_imgui()) {
      g_failed = true;
      return;
    }
    g_ready = true;
  }
  ImDrawData* dd = frame(w, h, &ImGui_ImplOpenGL3_NewFrame);
  if (!dd) return;
  StateGuard guard;
  ImGui_ImplOpenGL3_RenderDrawData(dd);
}

// The test harness's screenshot: the back buffer as it is about to be shown,
// panel included, as a binary PPM.
void save_shot(const std::string& path, unsigned w, unsigned h) {
  if (!gl.ReadPixels || !gl.ReadBuffer) return;
  constexpr GLenum kReadFramebuffer = 0x8CA8, kReadFramebufferBinding = 0x8CAA, kReadBufferEnum = 0x0C02,
                   kPackAlignment = 0x0D05, kPackRowLength = 0x0D02, kPixelPackBuffer = 0x88EB,
                   kPixelPackBufferBinding = 0x88ED, kBack = 0x0405, kRgb = 0x1907, kUnsignedByte = 0x1401;
  void (*bind_buffer)(GLenum, GLuint) = nullptr;
  load(bind_buffer, "glBindBuffer");
  GLint read_fb = 0, read_buf = 0, pack_align = 4, pack_row = 0, pack_buffer = 0;
  gl.GetIntegerv(kReadFramebufferBinding, &read_fb);
  gl.GetIntegerv(kReadBufferEnum, &read_buf);
  gl.GetIntegerv(kPackAlignment, &pack_align);
  gl.GetIntegerv(kPackRowLength, &pack_row);
  gl.GetIntegerv(kPixelPackBufferBinding, &pack_buffer);
  gl.BindFramebuffer(kReadFramebuffer, 0);
  gl.ReadBuffer(kBack);
  gl.PixelStorei(kPackAlignment, 1);
  gl.PixelStorei(kPackRowLength, 0);
  if (bind_buffer) bind_buffer(kPixelPackBuffer, 0);
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
  gl.ReadPixels(0, 0, static_cast<int>(w), static_cast<int>(h), kRgb, kUnsignedByte, px.data());
  if (bind_buffer) bind_buffer(kPixelPackBuffer, static_cast<GLuint>(pack_buffer));
  gl.PixelStorei(kPackRowLength, pack_row);
  gl.PixelStorei(kPackAlignment, pack_align);
  gl.ReadBuffer(static_cast<GLenum>(read_buf));
  gl.BindFramebuffer(kReadFramebuffer, static_cast<GLuint>(read_fb));
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned y = 0; y < h; ++y) std::fwrite(&px[static_cast<size_t>(h - 1 - y) * w * 3], 1, w * 3, f);
    std::fclose(f);
    logf("overlay: screenshot %s (%ux%u)", path.c_str(), w, h);
  }
}

void hooked_swap(Display* dpy, GLXDrawable drawable) {
  if (!g_failed) render(dpy, drawable);
  if (testkit::active() && g_ready) {
    const std::string shot = testkit::take_shot_request();
    if (!shot.empty()) {
      unsigned w = 0, h = 0;
      g_query_drawable(dpy, drawable, kGlxWidth, &w);
      g_query_drawable(dpy, drawable, kGlxHeight, &h);
      if (w && h) save_shot(shot, w, h);
    }
  }
  g_real_swap(dpy, drawable);
}

}  // namespace

bool install_presents() {
  // glvnd's libGLX.so.0 holds the glXSwapBuffers that libGL.so.1's forwards
  // to, so hooking it catches the game whichever library it called through.
  void* glx = dlopen("libGLX.so.0", RTLD_NOW | RTLD_LOCAL);
  const char* lib = "libGLX.so.0";
  if (!glx) {
    glx = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
    lib = "libGL.so.1";
  }
  if (!glx) {
    logf("overlay: no libGLX.so.0 or libGL.so.1 - no panel");
    return false;
  }
  g_query_drawable = reinterpret_cast<QueryDrawableFn>(dlsym(glx, "glXQueryDrawable"));
  g_current_context = reinterpret_cast<GetCurrentContextFn>(dlsym(glx, "glXGetCurrentContext"));
  g_get_proc = reinterpret_cast<GetProcFn>(dlsym(glx, "glXGetProcAddressARB"));
  void* swap = dlsym(glx, "glXSwapBuffers");
  if (!g_query_drawable || !g_current_context || !g_get_proc || !swap) {
    logf("overlay: %s lacks a GLX function the panel needs - no panel", lib);
    return false;
  }
  if (!hook::inline_hook(swap, reinterpret_cast<void*>(&hooked_swap), reinterpret_cast<void**>(&g_real_swap),
                         "glXSwapBuffers")) {
    logf("overlay: glXSwapBuffers (%s) could not be hooked - no panel", lib);
    return false;
  }
  logf("overlay: glXSwapBuffers hooked in %s", lib);
  return true;
}

}  // namespace ppcc::overlay
