// Inline hooks on the prologue shapes GameAssembly.so and libGLX.so use:
// endbr64 + push, a rip-relative compare first (IL2CPP's "method initialised"
// check), a short conditional jump inside the stolen bytes' reach, a call
// first - and, in the Windows build, MSVC's (GameAssembly.dll's): a register
// saved in the caller's home space, a push and a stack allocation. Each is
// hooked, called through the hook, and called through the trampoline (the
// original). The same test runs as tests/build/win/test_hook.exe under Wine.
//
//   make test
#include <cstdio>
#include <cstring>
#include <string>

#include "hook.h"
#include "log.h"

extern "C" {
int shape_endbr(int);
int shape_ripcmp(int);
int shape_jcc(int);
int shape_call(int);
int shape_msvc(int);
int helper_add100(int);
volatile char g_flag = 0;
}

// The first argument's register: rdi in the SysV ABI, rcx in Windows'.
#ifdef _WIN32
#define ARG "%rcx"
#define ARG32 "%ecx"
#else
#define ARG "%rdi"
#define ARG32 "%edi"
#endif

// Hand-written so the prologues are exactly the shapes under test.
asm(R"(
  .text
  .globl helper_add100
helper_add100:
  lea 100()" ARG R"(), %eax
  ret

  .globl shape_endbr
shape_endbr:
  endbr64
  push %rbp
  mov %rsp, %rbp
  lea 1()" ARG R"(), %eax
  pop %rbp
  ret

  .globl shape_ripcmp
shape_ripcmp:
  cmpb $0, g_flag(%rip)
  jne 1f
  lea 2()" ARG R"(), %eax
  ret
1:
  lea 3()" ARG R"(), %eax
  ret

  .globl shape_jcc
shape_jcc:
  test )" ARG32 ", " ARG32 R"(
  js 2f
  nop
  nop
  nop
  lea 4()" ARG R"(), %eax
  ret
2:
  mov $-4, %eax
  ret

  .globl shape_call
shape_call:
  push %rbx
  call helper_add100
  pop %rbx
  ret

  .globl shape_msvc
shape_msvc:
  mov %rbx, 8(%rsp)
  push %rdi
  sub $0x20, %rsp
  lea 5()" ARG R"(), %eax
  add $0x20, %rsp
  pop %rdi
  mov 8(%rsp), %rbx
  ret
)");

using Fn = int (*)(int);
static Fn g_orig[5];
static int g_calls[5];

template <int I>
static int detour(int x) {
  ++g_calls[I];
  return g_orig[I](x) * 10;
}

static int failures = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

int main() {
  ppcc::log_init("build/");  // run from tests/ (make test)
  Fn targets[5] = {shape_endbr, shape_ripcmp, shape_jcc, shape_call, shape_msvc};
  void* detours[5] = {reinterpret_cast<void*>(&detour<0>), reinterpret_cast<void*>(&detour<1>),
                      reinterpret_cast<void*>(&detour<2>), reinterpret_cast<void*>(&detour<3>),
                      reinterpret_cast<void*>(&detour<4>)};
  const char* names[5] = {"endbr64+push", "rip-relative cmp", "short jcc", "call rel32", "MSVC home-space save"};
  int expect_plain[5] = {6, 7, 9, 105, 10};  // f(5) before hooking
  // MSVC's shape stores into the caller's home space, which only Windows' ABI
  // gives a callee.
#ifdef _WIN32
  const int shapes = 5;
#else
  const int shapes = 4;
#endif
  for (int i = 0; i < shapes; ++i) {
    check(targets[i](5) == expect_plain[i], (std::string(names[i]) + ": unhooked result").c_str());
    const bool ok = ppcc::hook::inline_hook(reinterpret_cast<void*>(targets[i]), detours[i],
                                            reinterpret_cast<void**>(&g_orig[i]), names[i]);
    check(ok, (std::string(names[i]) + ": hooked").c_str());
    if (!ok) continue;
    check(targets[i](5) == expect_plain[i] * 10, (std::string(names[i]) + ": call goes through the detour").c_str());
    check(g_calls[i] == 1, (std::string(names[i]) + ": detour ran once").c_str());
    check(g_orig[i](5) == expect_plain[i], (std::string(names[i]) + ": trampoline runs the original").c_str());
  }
  // The rip-relative operand must still read the same variable from the trampoline.
  g_flag = 1;
  check(g_orig[1](5) == 8, "rip-relative cmp: relocated operand reads the original variable");
  // The relocated short jcc must still reach its target.
  check(g_orig[2](-1) == -4, "short jcc: relocated branch reaches its target");

  // A function too short to patch is refused, not broken.
  static const uint8_t tiny[] = {0x31, 0xC0, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
  uint8_t buf[64];
  size_t stolen = 0;
  std::string why;
  check(ppcc::hook::build_trampoline(tiny, 5, buf, sizeof(buf), &stolen, &why) == 0 && !why.empty(),
        "too-short function refused");
  std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
