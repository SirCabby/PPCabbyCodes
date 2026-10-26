#include "win/proxy.h"

#include <windows.h>

// Each export of hid.dll, in hid.def's order: D for the HidD_ functions, which
// report a failure as FALSE, P for the HidP_ ones, which return an NTSTATUS.
// The first 44 are every Windows' since XP (and Wine's); the last three are
// Windows 11's (HID API version 2), which an older Windows lacks (their slots
// fail there, where nothing can import them).
#define PPCC_HID_EXPORTS(X)                   \
  X(HidD_FlushQueue, D)                       \
  X(HidD_FreePreparsedData, D)                \
  X(HidD_GetAttributes, D)                    \
  X(HidD_GetConfiguration, D)                 \
  X(HidD_GetFeature, D)                       \
  X(HidD_GetHidGuid, D)                       \
  X(HidD_GetIndexedString, D)                 \
  X(HidD_GetInputReport, D)                   \
  X(HidD_GetManufacturerString, D)            \
  X(HidD_GetMsGenreDescriptor, D)             \
  X(HidD_GetNumInputBuffers, D)               \
  X(HidD_GetPhysicalDescriptor, D)            \
  X(HidD_GetPreparsedData, D)                 \
  X(HidD_GetProductString, D)                 \
  X(HidD_GetSerialNumberString, D)            \
  X(HidD_Hello, D)                            \
  X(HidD_SetConfiguration, D)                 \
  X(HidD_SetFeature, D)                       \
  X(HidD_SetNumInputBuffers, D)               \
  X(HidD_SetOutputReport, D)                  \
  X(HidP_GetButtonCaps, P)                    \
  X(HidP_GetCaps, P)                          \
  X(HidP_GetData, P)                          \
  X(HidP_GetExtendedAttributes, P)            \
  X(HidP_GetLinkCollectionNodes, P)           \
  X(HidP_GetScaledUsageValue, P)              \
  X(HidP_GetSpecificButtonCaps, P)            \
  X(HidP_GetSpecificValueCaps, P)             \
  X(HidP_GetUsageValue, P)                    \
  X(HidP_GetUsageValueArray, P)               \
  X(HidP_GetUsages, P)                        \
  X(HidP_GetUsagesEx, P)                      \
  X(HidP_GetValueCaps, P)                     \
  X(HidP_InitializeReportForID, P)            \
  X(HidP_MaxDataListLength, P)                \
  X(HidP_MaxUsageListLength, P)               \
  X(HidP_SetData, P)                          \
  X(HidP_SetScaledUsageValue, P)              \
  X(HidP_SetUsageValue, P)                    \
  X(HidP_SetUsageValueArray, P)               \
  X(HidP_SetUsages, P)                        \
  X(HidP_TranslateUsagesToI8042ScanCodes, P)  \
  X(HidP_UnsetUsages, P)                      \
  X(HidP_UsageListDifference, P)              \
  X(HidP_GetButtonArray, P)                   \
  X(HidP_SetButtonArray, P)                   \
  X(HidP_UsageAndPageListDifference, P)

// An export with no function behind it fails, and says why: FALSE (and the
// thread's last error) for a HidD_ function, HIDP_STATUS_NOT_IMPLEMENTED for a
// HidP_ one.
extern "C" UINT_PTR ppcc_hid_missing_D() {
  SetLastError(ERROR_PROC_NOT_FOUND);
  return 0;
}
extern "C" UINT_PTR ppcc_hid_missing_P() { return 0xC0110020; }

// The exports: `jmp *slot(%rip)`, a tail jump with every register and the
// stack as the caller left them, so one thunk serves any signature. Each slot
// starts at its failing stand-in, until load() points it at the real function.
#define PPCC_THUNK(name, kind)                                                            \
  extern "C" {                                                                            \
  void* ppcc_slot_##name = reinterpret_cast<void*>(&ppcc_hid_missing_##kind);             \
  }                                                                                       \
  asm(".text\n.globl ppcc_fwd_" #name "\nppcc_fwd_" #name ":\n\tjmp *ppcc_slot_" #name "(%rip)\n");
PPCC_HID_EXPORTS(PPCC_THUNK)
#undef PPCC_THUNK

namespace ppcc::proxy {
namespace {
bool g_loaded = false;
}

void load() {
  // System32's by its full path: by name alone Windows would hand back this DLL,
  // the hid.dll already loaded.
  wchar_t path[MAX_PATH];
  const UINT n = GetSystemDirectoryW(path, MAX_PATH);
  if (!n || n + 9 >= MAX_PATH) return;
  lstrcatW(path, L"\\hid.dll");
  HMODULE real = LoadLibraryW(path);
  if (!real) return;
#define PPCC_RESOLVE(name, kind)                                               \
  if (FARPROC fn = GetProcAddress(real, #name)) ppcc_slot_##name = reinterpret_cast<void*>(fn);
  PPCC_HID_EXPORTS(PPCC_RESOLVE)
#undef PPCC_RESOLVE
  g_loaded = true;
}

bool loaded() { return g_loaded; }

}  // namespace ppcc::proxy
