// Ghidra's non-returning function discovery takes IL2CPP's GC write barrier
// (il2cpp_gc_wbarrier_set_field: a card-table mark and ret) for a function that
// never returns, which cuts every decompiled method off at its first field
// store. This undoes it for the functions given (and their thunks): the flag,
// the call sites' flow overrides, the code after each call, and the callers'
// bodies.
//
//   analyzeHeadless ghidra_proj PP -process GameAssembly.so -noanalysis \
//       -scriptPath tools/ghidra -postScript FixNoReturn.java 0x780b00
//@category PPCabbyCodes
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class FixNoReturn extends GhidraScript {
  @Override
  public void run() throws Exception {
    for (String arg : getScriptArgs()) {
      Address addr = toAddr(Long.parseLong(arg.replace("0x", ""), 16));
      Function f = getFunctionAt(addr);
      if (f == null) {
        println("no function at " + addr);
        continue;
      }
      List<Function> targets = new ArrayList<>();
      targets.add(f);
      Address[] thunks = f.getFunctionThunkAddresses(true);
      if (thunks != null)
        for (Address t : thunks) {
          Function tf = getFunctionAt(t);
          if (tf != null) targets.add(tf);
        }
      Set<Function> callers = new HashSet<>();
      int sites = 0;
      for (Function t : targets) {
        t.setNoReturn(false);
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(t.getEntryPoint());
        while (refs.hasNext()) {
          Reference r = refs.next();
          if (!r.getReferenceType().isCall() && !r.getReferenceType().isJump()) continue;
          Instruction ins = getInstructionAt(r.getFromAddress());
          if (ins == null) continue;
          if (ins.getFlowOverride() != FlowOverride.NONE) ins.setFlowOverride(FlowOverride.NONE);
          Address next = ins.getMaxAddress().next();
          if (next != null && getInstructionAt(next) == null) disassemble(next);
          Function caller = getFunctionContaining(r.getFromAddress());
          if (caller != null) callers.add(caller);
          ++sites;
        }
      }
      for (Function c : callers) CreateFunctionCmd.fixupFunctionBody(currentProgram, c, monitor);
      println(f.getName() + ": " + targets.size() + " function(s), " + sites + " call sites, " + callers.size()
              + " callers repaired");
    }
  }
}
