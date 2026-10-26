// The hook engine's Windows side: code memory near a target (VirtualAlloc in
// a free region found with VirtualQuery, as MinHook finds one), code patching
// (VirtualProtect), a module's import address table, and pointer slots.
#include <windows.h>

#include <cstring>

#include "hook.h"

namespace ppcc::hook {
namespace {

constexpr int64_t kRel32Reach = 0x7FFF0000;

uintptr_t granularity() {
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  return si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
}

// A free region's start, stepping from `from` down to `lowest` (or up to
// `highest`), at the allocation granularity; 0 when there is none.
uintptr_t free_below(uintptr_t from, uintptr_t lowest, uintptr_t gran) {
  uintptr_t at = from - from % gran;
  if (at < gran) return 0;
  at -= gran;
  while (at >= lowest) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi))) return 0;
    if (mbi.State == MEM_FREE) return at;
    const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
    if (base < gran) return 0;
    at = base - gran;
  }
  return 0;
}

uintptr_t free_above(uintptr_t from, uintptr_t highest, uintptr_t gran) {
  uintptr_t at = from - from % gran + gran;
  while (at <= highest) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi))) return 0;
    if (mbi.State == MEM_FREE) return at;
    at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize + gran - 1;
    at -= at % gran;
  }
  return 0;
}

}  // namespace

uint8_t* alloc_code_near(uintptr_t around, size_t size) {
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const uintptr_t gran = granularity();
  const auto app_lo = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
  const auto app_hi = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
  const uintptr_t reach = static_cast<uintptr_t>(kRel32Reach) - (64u << 20);
  const uintptr_t lowest = around > app_lo + reach ? around - reach : app_lo;
  const uintptr_t highest = app_hi - around > reach ? around + reach : app_hi;
  // Below first (the gap before a module is usually free), then above; a
  // region another thread takes meanwhile is stepped over.
  for (int dir = 0; dir < 2; ++dir) {
    uintptr_t from = around;
    for (;;) {
      const uintptr_t at = dir == 0 ? free_below(from, lowest, gran) : free_above(from, highest, gran);
      if (!at) break;
      if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
        return static_cast<uint8_t*>(p);
      from = at;
    }
  }
  return nullptr;
}

bool seal_code(uint8_t* p, size_t size) {
  DWORD old = 0;
  return VirtualProtect(p, size, PAGE_EXECUTE_READ, &old) != 0;
}

void free_code(uint8_t* p, size_t) { VirtualFree(p, 0, MEM_RELEASE); }

bool patch_code(void* at, const void* bytes, size_t n) {
  DWORD old = 0;
  if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
  std::memcpy(at, bytes, n);
  VirtualProtect(at, n, old, &old);
  FlushInstructionCache(GetCurrentProcess(), at, n);
  return true;
}

void** iat_slot(void* module, const char* dll, const char* symbol) {
  auto* base = static_cast<uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return nullptr;
  for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
    // The import name table (OriginalFirstThunk) names each slot of the
    // address table (FirstThunk), which the loader has filled with addresses.
    if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0 || !d->OriginalFirstThunk) continue;
    const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + d->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
      const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), symbol) == 0)
        return reinterpret_cast<void**>(&slots->u1.Function);
    }
  }
  return nullptr;
}

bool write_slot(void** slot, void* value) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(slot, &mbi, sizeof(mbi))) return false;
  // A page that holds code too stays executable while it is written.
  const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), (mbi.Protect & exec) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old))
    return false;
  InterlockedExchangePointer(slot, value);
  VirtualProtect(slot, sizeof(void*), old, &old);
  return true;
}

bool slot_in_front(void** slot, void* hook, void** next) {
  if (!slot) return false;
  // A load, not a compare-exchange: that writes even when it fails, and the
  // slot's page is read-only.
  void* now = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
  if (now == hook) return false;
  // What the hook calls on to is in place before the hook is.
  InterlockedExchangePointer(next, now);
  return write_slot(slot, hook);
}

}  // namespace ppcc::hook
