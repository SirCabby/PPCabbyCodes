// The panel's renderer on Windows: Direct3D 11, which the game renders with
// ("Direct3D 11.0 [level 11.1]" in Player.log). UnityPlayer.dll loads dxgi.dll
// itself and makes its factory with CreateDXGIFactory2 (1, or the plain one, on
// an older Windows), looked up through GetProcAddress, where the mod sits
// (win/main.cpp). So the game's factory comes back through the mod, which hooks
// the factory class's swap chain calls, and each swap chain made for the
// game's window gets a vtable of its own: a copy of its class's, with the mod's
// Present, Present1 and ResizeBuffers(1) in it. The panel is drawn on the back
// buffer in Present, on the thread that presents (Unity's render thread).
//
// Why a copy of the swap chain's vtable rather than the class's: on Windows the
// Steam overlay hooks a swap chain by patching the start of the function each
// of its vtable slots points to, and its decoder does not know mingw's code -
// a function of the mod's in DXGI's class vtable crashed RE2CabbyCodes'
// players' games on their first frame (2026-09-25). No vtable another hooker
// reads holds a function of the mod's, and the mod calls on through the
// class's vtable as it is at the time: whatever hooked the class (the Steam
// overlay) gets every frame. A factory's class vtable is hooked in place
// instead (Wine's dxgi checks a factory's vtable pointer as it makes a swap
// chain, and the Steam overlay hooks no factory). This is RE2/RE4CabbyCodes'
// design (their overlay_d3d.cpp), for Direct3D 11 alone.
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx11.h"
#include "hook.h"
#include "imgui.h"
#include "log.h"
#include "overlay.h"
#include "platform.h"
#include "testkit.h"
#include "win/overlay_d3d11.h"

// ImGui's DX11 backend compiles its two shaders with D3DCompile. This stands in
// for d3dcompiler's export so the mod does not import the DLL: the game loads
// the mod at its start, where a missing DLL would stop it. It is loaded the
// first time the backend asks, and a system without it only loses the panel.
extern "C" HRESULT WINAPI D3DCompile(const void* data, SIZE_T data_size, const char* filename,
                                     const D3D_SHADER_MACRO* defines, ID3DInclude* include, const char* entrypoint,
                                     const char* target, UINT sflags, UINT eflags, ID3DBlob** shader,
                                     ID3DBlob** error_messages) {
  static pD3DCompile fn = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    for (const char* name : {"d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll"}) {
      HMODULE m = LoadLibraryA(name);
      if (!m) continue;
      fn = reinterpret_cast<pD3DCompile>(GetProcAddress(m, "D3DCompile"));
      if (fn) {
        ppcc::logf("overlay: shaders compiled with %s", name);
        break;
      }
    }
    if (!fn) ppcc::logf("overlay: no d3dcompiler_4x.dll - the panel cannot be drawn");
  }
  return fn ? fn(data, data_size, filename, defines, include, entrypoint, target, sflags, eflags, shader, error_messages)
            : E_FAIL;
}

namespace ppcc::overlay {
namespace {

// --- reading and writing other modules' memory --------------------------------------------------

// A read that fails, rather than faults, where nothing is mapped.
bool read_mem(const void* at, void* out, size_t n) {
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), at, out, n, &got) && got == n;
}

void** vtable_of(const void* obj) {
  void** vt = nullptr;
  return obj && read_mem(obj, &vt, sizeof(vt)) ? vt : nullptr;
}

HMODULE module_of(const void* p) {
  HMODULE m = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     static_cast<LPCSTR>(p), &m);
  return m;
}

// Whether a module is System32's dxgi.dll - Windows', Wine's, or DXVK's under
// Proton - told by its file: a wrapper in the game's folder (ReShade, Special K,
// OptiScaler: a dxgi.dll of their own) is not.
bool system_dxgi(HMODULE m) {
  wchar_t path[MAX_PATH], own[MAX_PATH];
  const UINT n = GetSystemDirectoryW(own, MAX_PATH);
  if (!m || !n || n + 10 >= MAX_PATH || !GetModuleFileNameW(m, path, MAX_PATH)) return false;
  lstrcatW(own, L"\\dxgi.dll");
  return lstrcmpiW(path, own) == 0;
}

// --- the swap chains, each with a vtable of its own ------------------------------------------------

// IDXGISwapChain3's slots the mod's copy has its own functions in.
constexpr int kPresent = 8, kResizeBuffers = 13, kPresent1 = 22, kResizeBuffers1 = 39;
constexpr int kHookedSlots[] = {kPresent, kResizeBuffers, kPresent1, kResizeBuffers1};
constexpr int kHooked = 4;
int hooked_index(int slot) {
  for (int i = 0; i < kHooked; ++i)
    if (kHookedSlots[i] == slot) return i;
  return -1;
}

struct Owned {
  void* obj = nullptr;       // the swap chain; stale once the game frees it (a new one there is adopted afresh)
  void** orig = nullptr;     // its class's vtable: what the hooks call on through
  void** copy = nullptr;     // its own (never freed: the object may outlive what the mod knows)
  int iface = 0;             // its interface's slots: 18, 29, 40 or 41 (IDXGISwapChain, 1, 3, 4)
  void* real[kHooked] = {};  // what the class had in the hooked slots when it was adopted
};
// A factory class's vtable, hooked in place: what its slots held.
struct FactoryVtable {
  void** vt = nullptr;
  int slots = 0;
  void* orig[32] = {};
};
constexpr int kChainsMax = 32, kFactoriesMax = 16;
Owned g_chains[kChainsMax];
FactoryVtable g_factories[kFactoriesMax];
int g_n_chains = 0, g_n_factories = 0;
std::mutex& owned_lock() {  // made on first use, never destroyed
  static std::mutex* m = new std::mutex;
  return *m;
}

DWORD g_game_thread = 0;  // the thread that loaded the mod: the game's main thread, which makes its window

Owned* find_owned(const void* obj) {  // under owned_lock
  for (int i = 0; i < g_n_chains; ++i)
    if (g_chains[i].obj == obj) return &g_chains[i];
  return nullptr;
}

Owned chain_of(const void* sc) {
  std::lock_guard<std::mutex> lock(owned_lock());
  const Owned* o = find_owned(sc);
  return o ? *o : Owned{};
}

// Proxies of a swap chain that a wrapper beside the exe hands out (ReShade,
// Special K, OptiScaler: a dxgi.dll in the game's folder), each known by an IID
// of its own. Never ask one which swap chain interfaces it has: Special K's
// QueryInterface for a higher one than it was made with hands the proxy back
// without adding a reference, and the Release that pairs with it destroys the
// swap chain under the game (an RE2CabbyCodes player's crash, 2026-09-25). A
// proxy is taken as IDXGISwapChain4, as all three are.
constexpr GUID kUnwrappedReShade = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};
constexpr GUID kUnwrappedSpecialK = {0xe8a33b4a, 0x1405, 0x424c, {0xae, 0x88, 0x0d, 0x3e, 0x9d, 0x46, 0xc9, 0x14}};
constexpr GUID kOptiScalerSwapChain = {0x3af622a3, 0x82d0, 0x49cd, {0x99, 0x4f, 0xcc, 0xe0, 0x51, 0x22, 0xc2, 0x22}};
const char* wrapper_of(IUnknown* obj) {
  const struct {
    const GUID* iid;
    const char* name;
  } known[] = {{&kUnwrappedSpecialK, "Special K"}, {&kUnwrappedReShade, "ReShade"}, {&kOptiScalerSwapChain, "OptiScaler"}};
  for (const auto& k : known) {
    IUnknown* inner = nullptr;
    if (SUCCEEDED(obj->QueryInterface(*k.iid, reinterpret_cast<void**>(&inner))) && inner) {
      inner->Release();
      return k.name;
    }
  }
  return nullptr;
}

// How many slots an object's vtable has: the highest interface it answers with
// the same vtable (one that hands back another object does not count).
struct Level {
  const IID* iid;
  int slots;
};
int slots_of(IUnknown* obj, void** vt, const Level* levels, int n, int least) {
  for (int i = 0; i < n; ++i) {
    IUnknown* p = nullptr;
    if (FAILED(obj->QueryInterface(*levels[i].iid, reinterpret_cast<void**>(&p))) || !p) continue;
    const bool same = vtable_of(p) == vt;
    p->Release();
    if (same) return levels[i].slots;
  }
  return least;
}

// Whether a swap chain made for this window is the game's: a window of this
// process made by the game's main thread, or of Unity's own class. Not a
// throwaway one of another tool's (for no window, a hidden one of its own).
bool game_window(HWND w) {
  DWORD pid = 0;
  const DWORD tid = w ? GetWindowThreadProcessId(w, &pid) : 0;
  if (!tid || pid != GetCurrentProcessId()) return false;
  if (tid == g_game_thread) return true;
  char cls[32] = {};
  return GetClassNameA(w, cls, sizeof(cls)) && std::strcmp(cls, "UnityWndClass") == 0;
}

// What a hook calls on to: the class's function as the class's vtable has it
// NOW (a tool that hooks the class after the swap chain was adopted still gets
// the game's frames), or what it had at the adoption while a call on to the
// class is already under way on this thread (a class hook that calls the
// object's vtable again must not come back into itself).
thread_local int t_calls = 0;
template <typename Fn>
Fn original(const void* sc, int slot) {
  const Owned o = chain_of(sc);
  const int i = hooked_index(slot);
  if (!o.orig || i < 0) return nullptr;
  void* now = nullptr;
  if (t_calls == 0 && slot < o.iface && read_mem(&o.orig[slot], &now, sizeof(now)) && now)
    return reinterpret_cast<Fn>(now);
  return reinterpret_cast<Fn>(o.real[i]);
}

struct CallingOn {
  CallingOn() { ++t_calls; }
  ~CallingOn() { --t_calls; }
};

// --- the renderer --------------------------------------------------------------------------------

struct Dx11 {
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  ID3D11DeviceContext1* ctx1 = nullptr;     // for SwapDeviceContextState
  ID3DDeviceContextState* state = nullptr;  // the panel's own pipeline state
  ID3D11RenderTargetView* rtv = nullptr;
  unsigned w = 0, h = 0;
  bool init = false, failed = false;
} g11;
IDXGISwapChain* g_sc = nullptr;  // the swap chain the panel draws on (the game's that presented last)

bool dx11_init(IDXGISwapChain* sc) {
  if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g11.dev))) || !g11.dev) {
    logf("overlay: the game's swap chain is not Direct3D 11's - no panel (the mod draws with Direct3D 11 only)");
    return false;
  }
  g11.dev->GetImmediateContext(&g11.ctx);
  // A pipeline state of the panel's own, swapped in around its drawing: Unity
  // caches what it has set on its context and does not set it again, so none
  // of it may change behind its back (ImGui's backend saves and restores most of
  // it, not all: the render targets and three shader stages it clears).
  ID3D11Device1* dev1 = nullptr;
  if (SUCCEEDED(g11.dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&dev1))) && dev1 &&
      SUCCEEDED(g11.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g11.ctx1))) &&
      g11.ctx1) {
    const D3D_FEATURE_LEVEL level = g11.dev->GetFeatureLevel();
    const UINT flags = (g11.dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                           ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                           : 0;
    if (FAILED(dev1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                              &g11.state)))
      g11.state = nullptr;
  }
  if (dev1) dev1->Release();
  create_context();
  if (!ImGui_ImplDX11_Init(g11.dev, g11.ctx)) {
    logf("overlay: ImGui's DX11 backend failed to start - no panel");
    ImGui::DestroyContext();
    return false;
  }
  logf("overlay: ImGui %s up on Direct3D 11 (feature level %#x)%s", IMGUI_VERSION,
       static_cast<unsigned>(g11.dev->GetFeatureLevel()),
       g11.state ? "" : " - no context state of its own: the panel saves the game's state around its drawing");
  return true;
}

void release_target() {
  if (g11.rtv) {
    g11.rtv->Release();
    g11.rtv = nullptr;
  }
}

bool make_target(IDXGISwapChain* sc) {
  ID3D11Texture2D* back = nullptr;
  if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) return false;
  D3D11_TEXTURE2D_DESC td{};
  back->GetDesc(&td);
  D3D11_RENDER_TARGET_VIEW_DESC rd{};
  rd.Format = td.Format;
  rd.ViewDimension = td.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
  const HRESULT hr = g11.dev->CreateRenderTargetView(back, &rd, &g11.rtv);
  back->Release();
  if (FAILED(hr)) {
    logf("overlay: no render target for the back buffer (format %d, hr 0x%08lX) - no panel", td.Format,
         static_cast<unsigned long>(hr));
    g11.failed = true;
    return false;
  }
  g11.w = td.Width;
  g11.h = td.Height;
  logf("overlay: drawing into the back buffer (%ux%u, format %d)", td.Width, td.Height, td.Format);
  return true;
}

void draw(IDXGISwapChain* sc) {
  if (g11.failed) return;
  if (!g11.init) {
    if (!dx11_init(sc)) {
      g11.failed = true;
      return;
    }
    g11.init = true;
  }
  if (!g11.rtv && !make_target(sc)) return;
  ImDrawData* dd = frame(g11.w, g11.h, &ImGui_ImplDX11_NewFrame);
  if (!dd) return;
  if (g11.state) {
    ID3DDeviceContextState* game = nullptr;
    g11.ctx1->SwapDeviceContextState(g11.state, &game);
    g11.ctx->OMSetRenderTargets(1, &g11.rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(dd);
    g11.ctx1->SwapDeviceContextState(game, nullptr);
    if (game) game->Release();
    return;
  }
  ID3D11RenderTargetView* old_rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
  ID3D11DepthStencilView* old_dsv = nullptr;
  g11.ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_rtv, &old_dsv);
  g11.ctx->OMSetRenderTargets(1, &g11.rtv, nullptr);
  ImGui_ImplDX11_RenderDrawData(dd);
  g11.ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_rtv, old_dsv);
  for (ID3D11RenderTargetView* r : old_rtv)
    if (r) r->Release();
  if (old_dsv) old_dsv->Release();
}

// The test harness's screenshot: the back buffer as it is about to be shown,
// panel included, as a binary PPM.
void save_shot(IDXGISwapChain* sc, const std::string& path) {
  ID3D11Texture2D* back = nullptr;
  if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) return;
  D3D11_TEXTURE2D_DESC d{};
  back->GetDesc(&d);
  const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  ID3D11Texture2D* staging = nullptr;
  D3D11_TEXTURE2D_DESC sd = d;
  sd.MipLevels = 1;
  sd.ArraySize = 1;
  sd.SampleDesc = {1, 0};
  sd.Usage = D3D11_USAGE_STAGING;
  sd.BindFlags = 0;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  sd.MiscFlags = 0;
  if ((!bgra && !rgba) || d.SampleDesc.Count > 1 || FAILED(g11.dev->CreateTexture2D(&sd, nullptr, &staging))) {
    logf("overlay: no screenshot of a back buffer of format %d (%u samples)", d.Format, d.SampleDesc.Count);
    back->Release();
    return;
  }
  g11.ctx->CopyResource(staging, back);
  back->Release();
  D3D11_MAPPED_SUBRESOURCE m{};
  if (SUCCEEDED(g11.ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
    std::vector<unsigned char> row(static_cast<size_t>(d.Width) * 3);
    if (FILE* f = platform::open_file(path, "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", d.Width, d.Height);
      for (unsigned y = 0; y < d.Height; ++y) {
        const auto* px = static_cast<const unsigned char*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
        for (unsigned x = 0; x < d.Width; ++x) {
          row[x * 3 + 0] = px[x * 4 + (bgra ? 2 : 0)];
          row[x * 3 + 1] = px[x * 4 + 1];
          row[x * 3 + 2] = px[x * 4 + (bgra ? 0 : 2)];
        }
        std::fwrite(row.data(), 1, row.size(), f);
      }
      std::fclose(f);
      logf("overlay: screenshot %s (%ux%u)", path.c_str(), d.Width, d.Height);
    }
    g11.ctx->Unmap(staging, 0);
  }
  staging->Release();
}

void on_present(IDXGISwapChain* sc) {
  if (sc != g_sc) {
    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(sc->GetDesc(&sd)) || !sd.OutputWindow || !IsWindowVisible(sd.OutputWindow)) return;
    logf("overlay: the game presents through swap chain %p (%ux%u, %u buffers, format %d, swap effect %d, window %p)%s",
         static_cast<void*>(sc), sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferCount, sd.BufferDesc.Format,
         sd.SwapEffect, static_cast<void*>(sd.OutputWindow), g_sc ? " - a new one: the panel moves to it" : "");
    release_target();
    if (g11.init) {
      // Another device's swap chain would need the backend made anew: the
      // panel's objects belong to the first device.
      ID3D11Device* dev = nullptr;
      const bool same = SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) && dev == g11.dev;
      if (dev) dev->Release();
      if (!same) {
        logf("overlay: the new swap chain is another device's - the panel stops until the game restarts");
        g11.failed = true;
      }
    }
    g_sc = sc;
  }
  draw(sc);
  if (testkit::active() && g11.init && !g11.failed) {
    const std::string shot = testkit::take_shot_request();
    if (!shot.empty()) save_shot(sc, shot);
  }
}

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*,
                                                     IUnknown* const*);

// A Present that comes back in while one is on its way on to the class (see
// original()) is the same frame: the panel is drawn the first time through.
HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
  if (t_calls == 0 && !(flags & DXGI_PRESENT_TEST)) on_present(sc);
  const PresentFn orig = original<PresentFn>(sc, kPresent);
  CallingOn calling;
  return orig ? orig(sc, sync, flags) : DXGI_ERROR_INVALID_CALL;
}

HRESULT STDMETHODCALLTYPE hk_present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
  if (t_calls == 0 && !(flags & DXGI_PRESENT_TEST)) on_present(sc);
  const Present1Fn orig = original<Present1Fn>(sc, kPresent1);
  CallingOn calling;
  return orig ? orig(sc, sync, flags, p) : DXGI_ERROR_INVALID_CALL;
}

// The back buffer's view goes before the buffers are made anew (a view left on
// one makes ResizeBuffers fail).
HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
  if (sc == g_sc) release_target();
  const ResizeBuffersFn orig = original<ResizeBuffersFn>(sc, kResizeBuffers);
  CallingOn calling;
  return orig ? orig(sc, n, w, h, f, flags) : DXGI_ERROR_INVALID_CALL;
}

HRESULT STDMETHODCALLTYPE hk_resize1(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags,
                                     const UINT* masks, IUnknown* const* queues) {
  if (sc == g_sc) release_target();
  const ResizeBuffers1Fn orig = original<ResizeBuffers1Fn>(sc, kResizeBuffers1);
  CallingOn calling;
  return orig ? orig(sc, n, w, h, f, flags, masks, queues) : DXGI_ERROR_INVALID_CALL;
}

// Points a swap chain at a private copy of its class's vtable with the hooks in
// it. The copy has 64 slots, more than any swap chain interface: a wrapper's
// proxy may keep a virtual destructor past the interface's slots (OptiScaler's
// does, and its Release calls it through the object's vtable pointer), and the
// slot before the first, where MSVC keeps a class's type information.
void adopt_chain(IUnknown* made, const char* how, HWND hwnd) {
  if (!game_window(hwnd)) {
    logf("overlay: a swap chain (%s) for a window not the game's (%p) is left alone", how, static_cast<void*>(hwnd));
    return;
  }
  auto* sc = static_cast<IDXGISwapChain*>(made);
  void** vt = vtable_of(sc);
  if (!vt) return;
  {
    // Ours already: DXGI's CreateSwapChain makes its swap chain through
    // CreateSwapChainForHwnd, so the object comes by here twice.
    std::lock_guard<std::mutex> lock(owned_lock());
    const Owned* o = find_owned(sc);
    if (o && o->copy == vt) return;
  }
  const char* wrapper = wrapper_of(sc);
  int iface;
  if (wrapper) {
    iface = 41;
  } else if (system_dxgi(module_of(vt))) {
    static const Level levels[] = {{&__uuidof(IDXGISwapChain4), 41}, {&__uuidof(IDXGISwapChain3), 40},
                                   {&__uuidof(IDXGISwapChain1), 29}};
    iface = slots_of(sc, vt, levels, 3, 18);
  } else {
    // Another kind of object, not asked (see wrapper_of): what made it promises
    // IDXGISwapChain (CreateSwapChain) or IDXGISwapChain1.
    iface = std::strcmp(how, "CreateSwapChain") == 0 ? 18 : 29;
  }
  constexpr int kCopySlots = 64;
  void** block = new void*[kCopySlots + 1]();
  void** copy = block + 1;
  // As many slots as are mapped (a vtable at the very end of its section), the
  // interface's at least.
  if (!read_mem(vt - 1, block, sizeof(void*) * (kCopySlots + 1))) {
    block[0] = nullptr;
    int slots = kCopySlots;
    while (slots > iface && !read_mem(vt, copy, sizeof(void*) * static_cast<size_t>(slots)))
      slots = slots - 8 > iface ? slots - 8 : iface;
    if (!read_mem(vt, copy, sizeof(void*) * static_cast<size_t>(slots))) {
      logf("overlay: the swap chain's vtable could not be read - no panel");
      delete[] block;
      return;
    }
  }
  static void* const hooks[kHooked] = {reinterpret_cast<void*>(&hk_present), reinterpret_cast<void*>(&hk_resize),
                                       reinterpret_cast<void*>(&hk_present1), reinterpret_cast<void*>(&hk_resize1)};
  Owned o;
  o.obj = sc;
  o.orig = vt;
  o.copy = copy;
  o.iface = iface;
  for (int i = 0; i < kHooked; ++i) {
    if (kHookedSlots[i] >= iface) continue;
    o.real[i] = vt[kHookedSlots[i]];
    copy[kHookedSlots[i]] = hooks[i];
  }
  {
    std::lock_guard<std::mutex> lock(owned_lock());
    Owned* slot = find_owned(sc);
    if (!slot) {
      if (g_n_chains >= kChainsMax) return;
      slot = &g_chains[g_n_chains++];
    }
    *slot = o;
    InterlockedExchangePointer(reinterpret_cast<void**>(sc), copy);
  }
  logf("overlay: the game made a swap chain (%s%s%s) - it has a vtable of its own now (%d slots its interface's)", how,
       wrapper ? ", a proxy of " : "", wrapper ? wrapper : "", iface);
}

// --- the factory -------------------------------------------------------------------------------

using QueryInterfaceFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, REFIID, void**);
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                    IDXGISwapChain1**);
constexpr int kQueryInterface = 0, kCreateSwapChain = 10, kCreateForHwnd = 15;  // IDXGIFactory2's vtable

void adopt_factory(IUnknown* factory, const char* how);

// A factory class's function, from the record of its hooked vtable (the
// object's vtable pointer names it).
template <typename Fn>
Fn factory_original(const void* self, int slot) {
  void** vt = vtable_of(self);
  std::lock_guard<std::mutex> lock(owned_lock());
  for (int i = 0; i < g_n_factories; ++i)
    if (g_factories[i].vt == vt && slot < g_factories[i].slots) return reinterpret_cast<Fn>(g_factories[i].orig[slot]);
  return nullptr;
}

bool is_factory(REFIID riid) {
  for (const IID* iid : {&__uuidof(IDXGIFactory), &__uuidof(IDXGIFactory1), &__uuidof(IDXGIFactory2),
                         &__uuidof(IDXGIFactory3), &__uuidof(IDXGIFactory4), &__uuidof(IDXGIFactory5),
                         &__uuidof(IDXGIFactory6), &__uuidof(IDXGIFactory7)})
    if (IsEqualIID(riid, *iid)) return true;
  return false;
}

HRESULT STDMETHODCALLTYPE hk_factory_query_interface(IUnknown* self, REFIID riid, void** out) {
  const auto orig = factory_original<QueryInterfaceFn>(self, kQueryInterface);
  if (!orig) return E_NOINTERFACE;
  const HRESULT hr = orig(self, riid, out);
  if (SUCCEEDED(hr) && out && *out && is_factory(riid)) adopt_factory(static_cast<IUnknown*>(*out), "QueryInterface");
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_swap_chain(IDXGIFactory* self, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                               IDXGISwapChain** out) {
  const auto orig = factory_original<CreateSwapChainFn>(self, kCreateSwapChain);
  if (!orig) return DXGI_ERROR_INVALID_CALL;
  const HWND hwnd = desc ? desc->OutputWindow : nullptr;
  const HRESULT hr = orig(self, device, desc, out);
  if (SUCCEEDED(hr) && out && *out) adopt_chain(*out, "CreateSwapChain", hwnd);
  return hr;
}

HRESULT STDMETHODCALLTYPE hk_create_for_hwnd(IDXGIFactory2* self, IUnknown* device, HWND hwnd,
                                             const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs,
                                             IDXGIOutput* restrict_to, IDXGISwapChain1** out) {
  const auto orig = factory_original<CreateForHwndFn>(self, kCreateForHwnd);
  if (!orig) return DXGI_ERROR_INVALID_CALL;
  const HRESULT hr = orig(self, device, hwnd, desc, fs, restrict_to, out);
  if (SUCCEEDED(hr) && out && *out) adopt_chain(*out, "CreateSwapChainForHwnd", hwnd);
  return hr;
}

void adopt_factory(IUnknown* factory, const char* how) {
  void** vt = vtable_of(factory);
  if (!vt) return;
  {
    std::lock_guard<std::mutex> lock(owned_lock());
    for (int i = 0; i < g_n_factories; ++i)
      if (g_factories[i].vt == vt) return;  // this class's vtable is hooked already
  }
  static const Level levels[] = {{&__uuidof(IDXGIFactory7), 32}, {&__uuidof(IDXGIFactory6), 30},
                                 {&__uuidof(IDXGIFactory5), 29}, {&__uuidof(IDXGIFactory4), 28},
                                 {&__uuidof(IDXGIFactory3), 26}, {&__uuidof(IDXGIFactory2), 25},
                                 {&__uuidof(IDXGIFactory1), 14}};
  const int slots = slots_of(factory, vt, levels, 7, 12);
  static const struct {
    int slot;
    void* fn;
  } hooks[] = {{kQueryInterface, reinterpret_cast<void*>(&hk_factory_query_interface)},
               {kCreateSwapChain, reinterpret_cast<void*>(&hk_create_swap_chain)},
               {kCreateForHwnd, reinterpret_cast<void*>(&hk_create_for_hwnd)}};
  std::lock_guard<std::mutex> lock(owned_lock());
  for (int i = 0; i < g_n_factories; ++i)
    if (g_factories[i].vt == vt) return;
  if (g_n_factories >= kFactoriesMax) return;
  FactoryVtable& f = g_factories[g_n_factories];
  f = FactoryVtable{vt, slots, {}};
  if (!read_mem(vt, f.orig, sizeof(void*) * static_cast<size_t>(slots))) return;
  ++g_n_factories;  // before the first slot is written: a call through a hooked slot finds its original
  int hooked = 0;
  for (const auto& h : hooks)
    if (h.slot < slots && hook::write_slot(&vt[h.slot], h.fn)) ++hooked;
  logf("overlay: the game's DXGI factory (%s, %d slots): %d slot(s) of its class vtable hooked in place", how, slots,
       hooked);
}

using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
CreateFactoryFn g_real_factory = nullptr, g_real_factory1 = nullptr;
CreateFactory2Fn g_real_factory2 = nullptr;

HRESULT WINAPI hk_create_factory(REFIID riid, void** out) {
  const HRESULT hr = g_real_factory(riid, out);
  if (SUCCEEDED(hr) && out && *out) adopt_factory(static_cast<IUnknown*>(*out), "CreateDXGIFactory");
  return hr;
}
HRESULT WINAPI hk_create_factory1(REFIID riid, void** out) {
  const HRESULT hr = g_real_factory1(riid, out);
  if (SUCCEEDED(hr) && out && *out) adopt_factory(static_cast<IUnknown*>(*out), "CreateDXGIFactory1");
  return hr;
}
HRESULT WINAPI hk_create_factory2(UINT flags, REFIID riid, void** out) {
  const HRESULT hr = g_real_factory2(flags, riid, out);
  if (SUCCEEDED(hr) && out && *out) adopt_factory(static_cast<IUnknown*>(*out), "CreateDXGIFactory2");
  return hr;
}

}  // namespace

bool install_presents() {
  // DllMain's thread: the game's main thread, which makes its window.
  g_game_thread = GetCurrentThreadId();
  logf("overlay: the game's DXGI factory will be taken as UnityPlayer.dll makes it");
  return true;
}

void* dxgi_export(const char* name, void* real) {
  if (std::strcmp(name, "CreateDXGIFactory") == 0) {
    g_real_factory = reinterpret_cast<CreateFactoryFn>(real);
    return reinterpret_cast<void*>(&hk_create_factory);
  }
  if (std::strcmp(name, "CreateDXGIFactory1") == 0) {
    g_real_factory1 = reinterpret_cast<CreateFactoryFn>(real);
    return reinterpret_cast<void*>(&hk_create_factory1);
  }
  if (std::strcmp(name, "CreateDXGIFactory2") == 0) {
    g_real_factory2 = reinterpret_cast<CreateFactory2Fn>(real);
    return reinterpret_cast<void*>(&hk_create_factory2);
  }
  return real;
}

}  // namespace ppcc::overlay
