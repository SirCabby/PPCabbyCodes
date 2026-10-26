#include "platform.h"

#include <windows.h>

// The SEH guard (platform::guarded): a frame whose unwind data names
// ppcc_seh_handler as its exception handler. For an MSVC C++ exception
// (0xE06D7363, how IL2CPP's code throws a managed exception) the handler unwinds
// to the guard's landing - the game's own frames on the way run their
// destructors, as MSVC's catch would have them - and the guard returns 1.
extern "C" {
int ppcc_seh_guard(void (*fn)(void*), void* arg);
void ppcc_seh_landing();

EXCEPTION_DISPOSITION ppcc_seh_handler(PEXCEPTION_RECORD rec, void* frame, PCONTEXT, PDISPATCHER_CONTEXT disp) {
  constexpr DWORD kMsvcCxx = 0xE06D7363;
  if (rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
  if (rec->ExceptionCode != kMsvcCxx) return ExceptionContinueSearch;
  RtlUnwindEx(frame, reinterpret_cast<PVOID>(&ppcc_seh_landing), rec, reinterpret_cast<PVOID>(1), disp->ContextRecord,
              disp->HistoryTable);
  return ExceptionContinueSearch;  // not reached
}
}

asm(R"(
    .text
    .globl ppcc_seh_guard
    .def ppcc_seh_guard; .scl 2; .type 32; .endef
    .seh_proc ppcc_seh_guard
ppcc_seh_guard:
    pushq %rbp
    .seh_pushreg %rbp
    movq %rsp, %rbp
    .seh_setframe %rbp, 0
    subq $32, %rsp
    .seh_stackalloc 32
    .seh_endprologue
    .seh_handler ppcc_seh_handler, @except
    movq %rcx, %rax
    movq %rdx, %rcx
    callq *%rax
    xorl %eax, %eax
1:  leaq 0(%rbp), %rsp
    popq %rbp
    retq
    .globl ppcc_seh_landing
ppcc_seh_landing:
    movl $1, %eax
    jmp 1b
    .seh_endproc
)");

namespace ppcc::platform {
namespace {

std::wstring wide(const std::string& utf8) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
  return w;
}

// The call inside the guard: the mod's own C++ exceptions are mingw's, which its
// catch sees; the game's go past it to the SEH handler.
struct Call {
  void (*fn)(void*);
  void* arg;
  bool ok;
};
void call_catching(void* p) {
  auto* c = static_cast<Call*>(p);
  try {
    c->fn(c->arg);
  } catch (...) {
    c->ok = false;
  }
}

}  // namespace

long thread_id() { return static_cast<long>(GetCurrentThreadId()); }

Time local_time() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  return Time{st.wHour, st.wMinute, st.wSecond, st.wMilliseconds};
}

FILE* open_file(const std::string& path, const char* mode) { return _wfopen(wide(path).c_str(), wide(mode).c_str()); }

bool replace_file(const std::string& from, const std::string& to) {
  return MoveFileExW(wide(from).c_str(), wide(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool remove_file(const std::string& path) { return DeleteFileW(wide(path).c_str()) != 0; }

void* symbol(void* library, const char* name) {
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

bool guarded(void (*fn)(void*), void* arg) {
  Call c{fn, arg, true};
  return ppcc_seh_guard(&call_catching, &c) == 0 && c.ok;
}

}  // namespace ppcc::platform
