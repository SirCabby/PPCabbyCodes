#!/bin/bash
# tools/ghidra/run.sh <dec|xref|find> [args...] - a headless query against the
# analysed GameAssembly.so (see tools/ghidra/Report.java and CLAUDE.md, "Tooling
# notes"). The answer is printed and also left in ghidra_proj/report.txt.
cd "$(dirname "$0")/../.." || exit 1
OUT="$PWD/ghidra_proj/report.txt"
MAXMEM=16G /opt/ghidra/support/analyzeHeadless ghidra_proj PP -process GameAssembly.so -noanalysis \
  -readOnly -scriptPath tools/ghidra -postScript Report.java "$OUT" "$@" > ghidra_proj/report.log 2>&1
cat "$OUT"
