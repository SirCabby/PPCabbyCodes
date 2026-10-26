// The hook engine's Linux side: code memory near a target (mmap), code
// patching (mprotect), and modules and their GOT slots (the dynamic loader's
// program headers and relocations).
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

#include "hook.h"
#include "log.h"

namespace ppcc::hook {
namespace {

constexpr int64_t kRel32Reach = 0x7FFF0000;

uintptr_t page_of(uintptr_t a) { return a & ~static_cast<uintptr_t>(getpagesize() - 1); }

}  // namespace

uint8_t* alloc_code_near(uintptr_t around, size_t size) {
  const size_t page = static_cast<size_t>(getpagesize());
  size = (size + page - 1) & ~(page - 1);
  // Try addresses moving away from `around`, 1 MB apart, first below (the
  // gap before a module is usually free), then above.
  const uintptr_t step = 1u << 20;
  const uintptr_t base = page_of(around) & ~(step - 1);
  for (uintptr_t dist = step; dist < static_cast<uintptr_t>(kRel32Reach) - (64u << 20); dist += step) {
    for (int dir = 0; dir < 2; ++dir) {
      const uintptr_t at = dir == 0 ? base - dist : base + dist;
      if (dir == 0 && dist > base) continue;
      void* p = mmap(reinterpret_cast<void*>(at), size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
      if (p == MAP_FAILED) continue;
      const int64_t d = static_cast<int64_t>(reinterpret_cast<uintptr_t>(p)) - static_cast<int64_t>(around);
      if (d > -kRel32Reach && d < kRel32Reach) return static_cast<uint8_t*>(p);
      munmap(p, size);  // an old kernel took the address as a hint only
    }
  }
  return nullptr;
}

bool seal_code(uint8_t* p, size_t size) {
  return mprotect(reinterpret_cast<void*>(page_of(reinterpret_cast<uintptr_t>(p))), size, PROT_READ | PROT_EXEC) == 0;
}

void free_code(uint8_t* p, size_t size) { munmap(p, size); }

bool patch_code(void* at, const void* bytes, size_t n) {
  const size_t page = static_cast<size_t>(getpagesize());
  const uintptr_t a = reinterpret_cast<uintptr_t>(at);
  const uintptr_t first = page_of(a), last = page_of(a + n - 1);
  const size_t span = last - first + page;
  if (mprotect(reinterpret_cast<void*>(first), span, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return false;
  std::memcpy(at, bytes, n);
  mprotect(reinterpret_cast<void*>(first), span, PROT_READ | PROT_EXEC);
  __builtin___clear_cache(static_cast<char*>(at), static_cast<char*>(at) + n);
  return true;
}

// --- modules and GOT slots ----------------------------------------------------

namespace {

struct FindModule {
  const char* file;
  Module* out;
  bool found;
  const ElfW(Phdr) * phdr;
  int phnum;
};

bool ends_with_file(const char* path, const char* file) {
  if (!path || !*path) return false;
  const size_t lp = std::strlen(path), lf = std::strlen(file);
  if (lp < lf || std::strcmp(path + lp - lf, file) != 0) return false;
  return lp == lf || path[lp - lf - 1] == '/';
}

int find_cb(dl_phdr_info* info, size_t, void* data) {
  auto* f = static_cast<FindModule*>(data);
  if (!ends_with_file(info->dlpi_name, f->file)) return 0;
  f->out->base = info->dlpi_addr;
  f->out->path = info->dlpi_name;
  f->phdr = info->dlpi_phdr;
  f->phnum = info->dlpi_phnum;
  f->found = true;
  return 1;
}

}  // namespace

bool find_module(const char* file_name, Module* out) {
  FindModule f{file_name, out, false, nullptr, 0};
  dl_iterate_phdr(find_cb, &f);
  return f.found;
}

bool got_hook(const char* module_file, const char* symbol, void* replacement, void** previous) {
  Module m;
  FindModule f{module_file, &m, false, nullptr, 0};
  dl_iterate_phdr(find_cb, &f);
  if (!f.found) {
    logf("got hook %s: module %s is not loaded", symbol, module_file);
    return false;
  }
  const ElfW(Dyn)* dyn = nullptr;
  uintptr_t relro_begin = 0, relro_end = 0;
  for (int i = 0; i < f.phnum; ++i) {
    if (f.phdr[i].p_type == PT_DYNAMIC) dyn = reinterpret_cast<const ElfW(Dyn)*>(m.base + f.phdr[i].p_vaddr);
    if (f.phdr[i].p_type == PT_GNU_RELRO) {
      relro_begin = m.base + f.phdr[i].p_vaddr;
      relro_end = relro_begin + f.phdr[i].p_memsz;
    }
  }
  if (!dyn) return false;
  // The loader may have turned these into absolute addresses in place.
  auto addr = [&](ElfW(Addr) a) { return a < m.base ? m.base + a : a; };
  const ElfW(Sym)* symtab = nullptr;
  const char* strtab = nullptr;
  const ElfW(Rela)* jmprel = nullptr;
  const ElfW(Rela)* rela = nullptr;
  size_t jmprel_size = 0, rela_size = 0;
  for (const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d) {
    switch (d->d_tag) {
      case DT_SYMTAB: symtab = reinterpret_cast<const ElfW(Sym)*>(addr(d->d_un.d_ptr)); break;
      case DT_STRTAB: strtab = reinterpret_cast<const char*>(addr(d->d_un.d_ptr)); break;
      case DT_JMPREL: jmprel = reinterpret_cast<const ElfW(Rela)*>(addr(d->d_un.d_ptr)); break;
      case DT_PLTRELSZ: jmprel_size = d->d_un.d_val; break;
      case DT_RELA: rela = reinterpret_cast<const ElfW(Rela)*>(addr(d->d_un.d_ptr)); break;
      case DT_RELASZ: rela_size = d->d_un.d_val; break;
      default: break;
    }
  }
  if (!symtab || !strtab) return false;
  int hooked = 0;
  auto scan = [&](const ElfW(Rela)* table, size_t size) {
    if (!table) return;
    for (size_t i = 0; i < size / sizeof(ElfW(Rela)); ++i) {
      const ElfW(Rela)& r = table[i];
      const unsigned type = ELF64_R_TYPE(r.r_info);
      if (type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT) continue;
      const ElfW(Sym)& s = symtab[ELF64_R_SYM(r.r_info)];
      if (std::strcmp(strtab + s.st_name, symbol) != 0) continue;
      auto* slot = reinterpret_cast<void**>(m.base + r.r_offset);
      const uintptr_t at = reinterpret_cast<uintptr_t>(slot);
      const bool in_relro = at >= relro_begin && at < relro_end;
      const uintptr_t pg = page_of(at);
      const size_t page = static_cast<size_t>(getpagesize());
      if (mprotect(reinterpret_cast<void*>(pg), page, PROT_READ | PROT_WRITE) != 0) continue;
      if (previous && !*previous) *previous = *slot;
      __atomic_store_n(slot, replacement, __ATOMIC_SEQ_CST);
      if (in_relro) mprotect(reinterpret_cast<void*>(pg), page, PROT_READ);
      ++hooked;
    }
  };
  scan(jmprel, jmprel_size);
  scan(rela, rela_size);
  if (!hooked) logf("got hook %s: %s does not import it", symbol, module_file);
  return hooked > 0;
}

}  // namespace ppcc::hook
