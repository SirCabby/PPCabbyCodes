// The Windows build (build/win/hid.dll), loaded by a program that is not the
// game: it must load, write no log, export exactly hid.dll's functions, and each
// must answer as Windows' own does - one that Windows' own lacks (Windows 11's
// HID API version 2 functions, on an older Windows or Wine) fails, saying why.
// Under Wine, run with WINEDLLOVERRIDES hid=n,b (make test sets it): Wine would
// load its own hid.dll in place of the file.
//
//   make test   (tests/build/win/test_proxy.exe, under Wine)
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

static bool exists(const char* path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

// The names a loaded module exports.
static std::set<std::string> exports_of(HMODULE m, DWORD* functions) {
  std::set<std::string> names;
  const auto* base = reinterpret_cast<const unsigned char*>(m);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dir.VirtualAddress) return names;
  const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
  const auto* list = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
  for (DWORD i = 0; i < exp->NumberOfNames; ++i) names.insert(reinterpret_cast<const char*>(base + list[i]));
  *functions = exp->NumberOfFunctions;
  return names;
}

int main() {
  // Run from tests/ (make test): the mod's log would go beside the exe.
  const char* log_here = "build\\win\\PPCabbyCodes.log";
  DeleteFileA(log_here);
  char path[MAX_PATH];
  GetFullPathNameA("..\\build\\win\\hid.dll", MAX_PATH, path, nullptr);
  HMODULE mod = LoadLibraryA(path);
  check(mod != nullptr, "the DLL loads");
  if (!mod) return 1;
  check(!exists(log_here) && !exists("PPCabbyCodes.log"), "outside the game it writes no log");

  DWORD functions = 0;
  const std::set<std::string> got = exports_of(mod, &functions);
  const std::set<std::string> want = {
      "HidD_FlushQueue", "HidD_FreePreparsedData", "HidD_GetAttributes", "HidD_GetConfiguration", "HidD_GetFeature",
      "HidD_GetHidGuid", "HidD_GetIndexedString", "HidD_GetInputReport", "HidD_GetManufacturerString",
      "HidD_GetMsGenreDescriptor", "HidD_GetNumInputBuffers", "HidD_GetPhysicalDescriptor", "HidD_GetPreparsedData",
      "HidD_GetProductString", "HidD_GetSerialNumberString", "HidD_Hello", "HidD_SetConfiguration", "HidD_SetFeature",
      "HidD_SetNumInputBuffers", "HidD_SetOutputReport", "HidP_GetButtonCaps", "HidP_GetCaps", "HidP_GetData",
      "HidP_GetExtendedAttributes", "HidP_GetLinkCollectionNodes", "HidP_GetScaledUsageValue",
      "HidP_GetSpecificButtonCaps", "HidP_GetSpecificValueCaps", "HidP_GetUsageValue", "HidP_GetUsageValueArray",
      "HidP_GetUsages", "HidP_GetUsagesEx", "HidP_GetValueCaps", "HidP_InitializeReportForID",
      "HidP_MaxDataListLength", "HidP_MaxUsageListLength", "HidP_SetData", "HidP_SetScaledUsageValue",
      "HidP_SetUsageValue", "HidP_SetUsageValueArray", "HidP_SetUsages", "HidP_TranslateUsagesToI8042ScanCodes",
      "HidP_UnsetUsages", "HidP_UsageListDifference", "HidP_GetButtonArray", "HidP_SetButtonArray",
      "HidP_UsageAndPageListDifference"};
  check(got == want && functions == want.size(), "it exports exactly hid.dll's 47 functions");

  wchar_t sys_dir[MAX_PATH];
  GetSystemDirectoryW(sys_dir, MAX_PATH);
  HMODULE real = LoadLibraryW((std::wstring(sys_dir) + L"\\hid.dll").c_str());
  check(real != nullptr && real != mod, "Windows' own hid.dll is another module");

  // Two signatures: a GUID filled in (no result), a BOOLEAN from a handle and a
  // structure.
  using GuidFn = void(WINAPI*)(GUID*);
  struct Attributes {
    ULONG size;
    USHORT vendor, product, version;
  };
  using AttributesFn = BOOLEAN(WINAPI*)(HANDLE, Attributes*);
  const auto guid = reinterpret_cast<GuidFn>(GetProcAddress(mod, "HidD_GetHidGuid"));
  const auto real_guid = reinterpret_cast<GuidFn>(GetProcAddress(real, "HidD_GetHidGuid"));
  const auto attributes = reinterpret_cast<AttributesFn>(GetProcAddress(mod, "HidD_GetAttributes"));
  check(guid && attributes && guid != real_guid, "its functions are its own (jumps on)");
  GUID ours{}, theirs{};
  guid(&ours);
  real_guid(&theirs);
  check(std::memcmp(&ours, &theirs, sizeof(GUID)) == 0 && ours.Data1 == 0x4d1e55b2,
        "HidD_GetHidGuid answers as Windows' own (the HID class GUID)");
  Attributes a{sizeof(Attributes), 0, 0, 0};
  check(!attributes(INVALID_HANDLE_VALUE, &a), "HidD_GetAttributes of no device fails, as Windows' own does");

  using StatusFn = LONG(WINAPI*)();
  const auto button_array = reinterpret_cast<StatusFn>(GetProcAddress(mod, "HidP_GetButtonArray"));
  if (!GetProcAddress(real, "HidP_GetButtonArray")) {
    check(button_array && static_cast<ULONG>(button_array()) == 0xC0110020,
          "an export Windows' own lacks fails, with HIDP_STATUS_NOT_IMPLEMENTED");
  } else {
    std::printf("SKIP this hid.dll has HidP_GetButtonArray\n");
  }

  check(FreeLibrary(mod) != 0, "it unloads");
  check(!exists(log_here), "and wrote no log");
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
