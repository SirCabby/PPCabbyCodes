// The built library, loaded by something that is not the game (as a tool or a
// test would): it must load, do nothing - no log, no hooks - and export no
// symbol that could stand in for another library's. And it must unwind with
// the process's own libgcc_s, as GameAssembly.so throws through it: with an
// unwinder of its own (-static-libgcc) the mod's catch aborted the game.
//
//   make test
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// Defined symbols in the library's dynamic symbol table.
static int exported_symbols(void* handle) {
  link_map* map = nullptr;
  if (dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0 || !map) return -1;
  const ElfW(Sym)* symtab = nullptr;
  const char* strtab = nullptr;
  const ElfW(Word)* gnu_hash = nullptr;
  for (const ElfW(Dyn)* d = map->l_ld; d->d_tag != DT_NULL; ++d) {
    if (d->d_tag == DT_SYMTAB) symtab = reinterpret_cast<const ElfW(Sym)*>(d->d_un.d_ptr);
    if (d->d_tag == DT_STRTAB) strtab = reinterpret_cast<const char*>(d->d_un.d_ptr);
    if (d->d_tag == DT_GNU_HASH) gnu_hash = reinterpret_cast<const ElfW(Word)*>(d->d_un.d_ptr);
  }
  if (!symtab || !strtab || !gnu_hash) return -1;
  // The GNU hash table's symbols are the exported ones: count them.
  const ElfW(Word) nbuckets = gnu_hash[0], symoffset = gnu_hash[1], bloom = gnu_hash[2];
  const ElfW(Word)* buckets = gnu_hash + 4 + bloom * (sizeof(ElfW(Addr)) / 4);
  const ElfW(Word)* chain = buckets + nbuckets;
  ElfW(Word) last = 0;
  for (ElfW(Word) b = 0; b < nbuckets; ++b)
    if (buckets[b] > last) last = buckets[b];
  if (last < symoffset) return 0;
  while (!(chain[last - symoffset] & 1)) ++last;
  int n = 0;
  for (ElfW(Word) i = symoffset; i <= last; ++i)
    if (symtab[i].st_shndx != SHN_UNDEF) {
      std::printf("  exported: %s\n", strtab + symtab[i].st_name);
      ++n;
    }
  return n;
}

int main() {
  std::remove("build/PPCabbyCodes.log");
  void* mod = dlopen("../build/libPPCabbyCodes.so", RTLD_NOW | RTLD_LOCAL);
  check(mod != nullptr, "the library loads");
  if (!mod) {
    std::printf("  dlerror: %s\n", dlerror());
    return 1;
  }
  check(access("build/PPCabbyCodes.log", F_OK) != 0 && access("../PPCabbyCodes.log", F_OK) != 0,
        "outside the game it writes no log");
  check(exported_symbols(mod) == 0, "it exports nothing");
  void* gcc_s = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_NOLOAD);
  void* set_gr = dlsym(mod, "_Unwind_SetGR");  // the library's own copy is never found this way
  check(gcc_s && set_gr && set_gr == dlsym(gcc_s, "_Unwind_SetGR"),
        "its catch unwinds with the process's libgcc_s (no unwinder of its own)");
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
