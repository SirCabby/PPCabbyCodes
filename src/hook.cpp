#include "hook.h"

#include <cstdio>
#include <cstring>

#include "../contrib/hde/hde64.h"
#include "log.h"

namespace ppcc::hook {
namespace {

constexpr size_t kPatchLen = 5;  // E9 rel32

bool fits_rel32(int64_t d) { return d >= INT32_MIN && d <= INT32_MAX; }

// The relocator's output buffer.
struct Out {
  uint8_t* base;
  size_t cap;
  size_t len = 0;
  bool overflow = false;
  uintptr_t here() const { return reinterpret_cast<uintptr_t>(base) + len; }
  void put(const void* p, size_t n) {
    if (len + n > cap) {
      overflow = true;
      return;
    }
    std::memcpy(base + len, p, n);
    len += n;
  }
  void u8(uint8_t b) { put(&b, 1); }
  void u32(uint32_t v) { put(&v, 4); }
  void u64(uint64_t v) { put(&v, 8); }
  // jmp qword [rip+0]; dq dest - reaches anywhere.
  void jmp_abs(uintptr_t dest) {
    const uint8_t op[6] = {0xFF, 0x25, 0, 0, 0, 0};
    put(op, 6);
    u64(dest);
  }
  // jmp rel32 when it reaches, else the absolute form.
  void jmp(uintptr_t dest) {
    const int64_t d = static_cast<int64_t>(dest) - static_cast<int64_t>(here() + 5);
    if (fits_rel32(d)) {
      u8(0xE9);
      u32(static_cast<uint32_t>(d));
    } else {
      jmp_abs(dest);
    }
  }
};

}  // namespace

size_t build_trampoline(const uint8_t* target, size_t min_len, uint8_t* out_buf, size_t cap, size_t* stolen,
                        std::string* why) {
  Out out{out_buf, cap};
  const uintptr_t start = reinterpret_cast<uintptr_t>(target);
  uintptr_t src = start;
  auto fail = [&](const char* fmt, uintptr_t at) {
    if (why) {
      char msg[160];
      std::snprintf(msg, sizeof(msg), fmt, static_cast<unsigned long>(at - start));
      *why = msg;
    }
    return static_cast<size_t>(0);
  };
  for (;;) {
    if (src - start >= min_len) {
      out.jmp(src);  // continue in the original after the stolen bytes
      break;
    }
    hde64s hs{};
    const unsigned len = hde64_disasm(reinterpret_cast<const void*>(src), &hs);
    if ((hs.flags & F_ERROR) || len == 0) return fail("undecodable instruction at +0x%lx", src);
    const auto* insn = reinterpret_cast<const uint8_t*>(src);
    const uintptr_t next = src + len;
    // A branch back into the bytes being replaced cannot be relocated.
    auto lands_inside = [&](uintptr_t dest) { return dest > start && dest < start + kPatchLen; };

    if (hs.opcode == 0xC3 || hs.opcode == 0xC2 || hs.opcode == 0xCC) {
      return fail("function ends (ret/int3) at +0x%lx, before the 5 bytes a jump needs", src);
    }
    if ((hs.opcode & 0xFC) == 0xE0) return fail("loop/jrcxz at +0x%lx", src);

    if (hs.opcode == 0xE8) {  // call rel32
      const uintptr_t dest = next + static_cast<int32_t>(hs.imm.imm32);
      const int64_t d = static_cast<int64_t>(dest) - static_cast<int64_t>(out.here() + 5);
      if (fits_rel32(d)) {
        out.u8(0xE8);
        out.u32(static_cast<uint32_t>(d));
      } else {
        // call qword [rip+2]; jmp +8; dq dest
        const uint8_t op[8] = {0xFF, 0x15, 0x02, 0, 0, 0, 0xEB, 0x08};
        out.put(op, 8);
        out.u64(dest);
      }
    } else if (hs.opcode == 0xE9 || hs.opcode == 0xEB) {  // jmp rel32 / rel8
      const uintptr_t dest =
          next + (hs.opcode == 0xEB ? static_cast<int8_t>(hs.imm.imm8) : static_cast<int32_t>(hs.imm.imm32));
      if (lands_inside(dest)) return fail("jump back into the patched bytes at +0x%lx", src);
      out.jmp(dest);  // the function continues there: nothing after it is needed
      src = next;
      break;
    } else if ((hs.opcode & 0xF0) == 0x70 || (hs.opcode == 0x0F && (hs.opcode2 & 0xF0) == 0x80)) {  // jcc
      const bool short_form = (hs.opcode & 0xF0) == 0x70;
      const uint8_t cond = short_form ? (hs.opcode & 0x0F) : (hs.opcode2 & 0x0F);
      const uintptr_t dest =
          next + (short_form ? static_cast<int8_t>(hs.imm.imm8) : static_cast<int32_t>(hs.imm.imm32));
      if (lands_inside(dest)) return fail("conditional jump back into the patched bytes at +0x%lx", src);
      const int64_t d = static_cast<int64_t>(dest) - static_cast<int64_t>(out.here() + 6);
      if (fits_rel32(d)) {
        out.u8(0x0F);
        out.u8(static_cast<uint8_t>(0x80 | cond));
        out.u32(static_cast<uint32_t>(d));
      } else {
        // the opposite condition jumps over an absolute jmp
        out.u8(static_cast<uint8_t>(0x70 | (cond ^ 1)));
        out.u8(14);
        out.jmp_abs(dest);
      }
    } else if ((hs.flags & F_MODRM) && (hs.modrm & 0xC7) == 0x05) {  // [rip+disp32]
      uint8_t buf[16];
      std::memcpy(buf, insn, len);
      const unsigned imm_len = (hs.flags & 0x3C) >> 2;  // F_IMM8..F_IMM64 -> 1, 2, 4, 8
      const unsigned disp_at = len - imm_len - 4;
      const uintptr_t abs = next + static_cast<int32_t>(hs.disp.disp32);
      const int64_t d = static_cast<int64_t>(abs) - static_cast<int64_t>(out.here() + len);
      if (!fits_rel32(d)) return fail("rip-relative operand out of the trampoline's reach at +0x%lx", src);
      const int32_t d32 = static_cast<int32_t>(d);
      std::memcpy(buf + disp_at, &d32, 4);
      out.put(buf, len);
      if (hs.opcode == 0xFF && hs.modrm_reg == 4) {  // jmp qword [rip+x]: nothing follows
        src = next;
        break;
      }
    } else if (hs.flags & F_RELATIVE) {
      return fail("relative branch the relocator does not know at +0x%lx", src);
    } else {
      if (hs.opcode == 0xFF && hs.modrm_reg == 4) {  // jmp through a register or memory
        out.put(insn, len);
        src = next;
        break;
      }
      out.put(insn, len);
    }
    if (out.overflow) return fail("trampoline buffer too small at +0x%lx", src);
    src = next;
  }
  if (out.overflow) return fail("trampoline buffer too small at +0x%lx", src);
  if (src - start < kPatchLen) return fail("only 0x%lx bytes before the function leaves", src);
  if (stolen) *stolen = src - start;
  return out.len;
}

bool inline_hook(void* target, void* detour, void** original, const char* name) {
  if (!target || !detour) {
    logf("hook %s: no %s", name, target ? "detour" : "target");
    return false;
  }
  const auto t = reinterpret_cast<uintptr_t>(target);
  constexpr size_t kCode = 4096;
  uint8_t* mem = alloc_code_near(t, kCode);
  if (!mem) {
    logf("hook %s: no free memory within 2 GB of %p for the trampoline", name, target);
    return false;
  }
  // The block: the relay to the detour at 0, the trampoline at 16.
  constexpr size_t kRelay = 0, kTramp = 16;
  Out relay{mem + kRelay, 14};
  relay.jmp_abs(reinterpret_cast<uintptr_t>(detour));
  size_t stolen = 0;
  std::string why;
  const size_t n = build_trampoline(static_cast<const uint8_t*>(target), kPatchLen, mem + kTramp, kCode - kTramp,
                                    &stolen, &why);
  if (n == 0) {
    logf("hook %s: not hooked - %s", name, why.c_str());
    free_code(mem, kCode);
    return false;
  }
  seal_code(mem, kCode);

  uint8_t patch[kPatchLen];
  patch[0] = 0xE9;
  const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(reinterpret_cast<uintptr_t>(mem + kRelay)) -
                                           static_cast<int64_t>(t + kPatchLen));
  std::memcpy(patch + 1, &rel, 4);
  // The trampoline is ready before the jump to the detour can be taken.
  if (original) *original = mem + kTramp;
  if (!patch_code(target, patch, kPatchLen)) {
    logf("hook %s: cannot make %p writable", name, target);
    if (original) *original = nullptr;
    free_code(mem, kCode);
    return false;
  }
  logf("hook %s: %p -> %p (%zu bytes moved, trampoline %p)", name, target, detour, stolen,
       static_cast<void*>(mem + kTramp));
  return true;
}

}  // namespace ppcc::hook
