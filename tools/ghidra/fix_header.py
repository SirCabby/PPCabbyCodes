#!/usr/bin/env python3
"""Make Il2CppDumper's il2cpp.h parseable by Ghidra's C parser (as the
dumper's own il2cpp_header_to_ghidra.py does): fixed-width typedefs up front,
and C++ base classes turned into a leading 'super' member.

    python3 tools/ghidra/fix_header.py <il2cpp.h> <il2cpp_ghidra.h>
"""
import re
import sys

HEADER = """typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef __int64 intptr_t;
typedef __int64 uintptr_t;
typedef unsigned __int64 size_t;
typedef _Bool bool;
"""

def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as f:
        data = f.read()
    data = re.sub(r": (\w+) {", r"{\n \1 super;", data)
    with open(dst, "w", encoding="utf-8") as f:
        f.write(HEADER)
        f.write(data)

if __name__ == "__main__":
    main()
