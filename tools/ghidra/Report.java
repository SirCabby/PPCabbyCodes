// Headless queries over the analysed GameAssembly.so (tools/ghidra/run.sh):
//
//   dec  <name|0xaddr>...   decompile each function (a name as Il2CppDumper gives
//                           it, e.g. play.day.booth.Booth$$update)
//   xref <name|0xaddr>...   every reference to each function, with the function
//                           it comes from
//   find <substring>...     every function whose name contains the substring
//   grep <substring> <regex> decompile every function whose name contains the
//                           substring and print the lines matching the regex
//                           (who calls a virtual method: grep for its slot name)
//
// "d:", "x:" and "f:" in front of one argument switch the mode for that
// argument only, so one run (each run reopens the project) can ask several
// questions. The image base is 0: addresses are the RVAs of dump.cs.
//@category PPCabbyCodes
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class Report extends GhidraScript {
  private DecompInterface ifc;
  private PrintWriter out;

  private void say(String s) { out.println(s); }

  private List<Function> resolve(String arg) {
    List<Function> fs = new ArrayList<>();
    if (arg.startsWith("0x") || arg.startsWith("0X")) {
      Address a = toAddr(Long.parseLong(arg.substring(2), 16));
      Function f = getFunctionContaining(a);
      if (f != null) fs.add(f);
      return fs;
    }
    SymbolIterator it = currentProgram.getSymbolTable().getSymbols(arg);
    while (it.hasNext()) {
      Symbol s = it.next();
      Function f = getFunctionAt(s.getAddress());
      if (f != null && !fs.contains(f)) fs.add(f);
    }
    if (fs.isEmpty()) {
      for (Function f : currentProgram.getFunctionManager().getFunctions(true))
        if (f.getName().equals(arg)) fs.add(f);
    }
    return fs;
  }

  private void decompile(Function f) {
    DecompileResults r = ifc.decompileFunction(f, 180, monitor);
    say("=== " + f.getName() + " @ " + f.getEntryPoint() + " ===");
    if (r != null && r.getDecompiledFunction() != null) say(r.getDecompiledFunction().getC());
    else say("(decompilation failed: " + (r != null ? r.getErrorMessage() : "?") + ")");
  }

  private void xref(Function f) {
    say("=== references to " + f.getName() + " @ " + f.getEntryPoint() + " ===");
    ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(f.getEntryPoint());
    int n = 0;
    while (refs.hasNext()) {
      Reference r = refs.next();
      Address from = r.getFromAddress();
      Function g = getFunctionContaining(from);
      say("  " + from + " " + r.getReferenceType() + (g != null ? "  in " + g.getName() + " @ " + g.getEntryPoint() : ""));
      ++n;
    }
    if (n == 0) say("  (none)");
  }

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      println("usage: Report.java <out-file> <dec|xref|find> [args...]");
      return;
    }
    ifc = new DecompInterface();
    DecompileOptions opts = new DecompileOptions();
    ifc.setOptions(opts);
    ifc.openProgram(currentProgram);
    out = new PrintWriter(args[0]);
    String mode = args[1];
    if (mode.equals("grep")) {
      java.util.regex.Pattern re = java.util.regex.Pattern.compile(args[3]);
      FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
      while (it.hasNext()) {
        Function f = it.next();
        if (!f.getName().contains(args[2])) continue;
        DecompileResults r = ifc.decompileFunction(f, 120, monitor);
        if (r == null || r.getDecompiledFunction() == null) continue;
        String[] lines = r.getDecompiledFunction().getC().split("\n");
        boolean header = false;
        for (String line : lines) {
          if (!re.matcher(line).find()) continue;
          if (!header) {
            say("=== " + f.getName() + " @ " + f.getEntryPoint() + " ===");
            header = true;
          }
          say("  " + line.trim());
        }
      }
      out.close();
      ifc.dispose();
      return;
    }
    for (int i = 2; i < args.length; ++i) {
      String arg = args[i];
      String m = mode;
      if (arg.length() > 2 && arg.charAt(1) == ':') {
        char c = arg.charAt(0);
        m = c == 'x' ? "xref" : c == 'f' ? "find" : "dec";
        arg = arg.substring(2);
      }
      if (m.equals("find")) {
        say("=== functions named like " + arg + " ===");
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        while (it.hasNext()) {
          Function f = it.next();
          if (f.getName().contains(arg)) say("  " + f.getEntryPoint() + " " + f.getName());
        }
        continue;
      }
      List<Function> fs = resolve(arg);
      if (fs.isEmpty() && m.equals("xref")) {
        // Not a function: a data label (a string literal's slot, a type's
        // metadata) - who reads it.
        SymbolIterator syms = currentProgram.getSymbolTable().getSymbols(arg);
        boolean any = false;
        while (syms.hasNext()) {
          Symbol sym = syms.next();
          any = true;
          say("=== references to " + sym.getName() + " @ " + sym.getAddress() + " ===");
          ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(sym.getAddress());
          while (refs.hasNext()) {
            Reference r = refs.next();
            Function g = getFunctionContaining(r.getFromAddress());
            say("  " + r.getFromAddress() + " " + r.getReferenceType() + (g != null ? "  in " + g.getName() : ""));
          }
        }
        if (any) continue;
      }
      if (fs.isEmpty()) {
        say("=== " + arg + ": no such function ===");
        continue;
      }
      for (Function f : fs) {
        if (m.equals("xref")) xref(f);
        else decompile(f);
      }
    }
    out.close();
    ifc.dispose();
  }
}
