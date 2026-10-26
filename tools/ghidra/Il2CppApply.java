// Applies Il2CppDumper's output to GameAssembly.so before analysis: the C types
// of il2cpp.h (made Ghidra-parseable by tools/ghidra/fix_header.py), a function
// at every method address, the method names and signatures, and labels on the
// string literal and metadata slots.
//
//   analyzeHeadless ghidra_proj PP -import GameAssembly.so -loader ElfLoader \
//       -loader-imagebase 0 -scriptPath tools/ghidra \
//       -preScript Il2CppApply.java <script.json> <il2cpp_ghidra.h>
//
// The image base is 0 so every address in Ghidra is the RVA dump.cs prints.
//@category PPCabbyCodes
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParser;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.FileInputStream;
import java.io.FileReader;

public class Il2CppApply extends GhidraScript {
  private Address at(long rva) { return currentProgram.getImageBase().add(rva); }

  private void label(Address a, String name) {
    try {
      createLabel(a, name.replace(' ', '-'), true, SourceType.USER_DEFINED);
    } catch (Exception e) {
      // a name Ghidra will not take; the address keeps its default label
    }
  }

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      println("usage: Il2CppApply.java <script.json> <il2cpp_ghidra.h>");
      return;
    }
    DataTypeManager dtm = currentProgram.getDataTypeManager();
    println("parsing " + args[1]);
    CParser parser = new CParser(dtm, true, null);
    try (FileInputStream in = new FileInputStream(args[1])) {
      parser.parse(in);
    }
    println("header parsed: " + parser.getComposites().size() + " composites");

    JsonObject root = JsonParser.parseReader(new FileReader(args[0])).getAsJsonObject();

    JsonArray addrs = root.getAsJsonArray("Addresses");
    println("functions: " + addrs.size());
    for (int i = 0; i + 1 < addrs.size(); ++i) {
      Address a = at(addrs.get(i).getAsLong());
      if (getFunctionAt(a) != null) continue;
      disassemble(a);
      createFunction(a, null);
    }

    JsonArray methods = root.getAsJsonArray("ScriptMethod");
    println("methods: " + methods.size());
    for (JsonElement e : methods) {
      JsonObject m = e.getAsJsonObject();
      Address a = at(m.get("Address").getAsLong());
      String name = m.get("Name").getAsString().replace(' ', '-');
      Function f = getFunctionAt(a);
      if (f == null) {
        disassemble(a);
        f = createFunction(a, name);
      }
      label(a, name);
    }

    JsonArray strings = root.getAsJsonArray("ScriptString");
    int idx = 1;
    for (JsonElement e : strings) {
      JsonObject s = e.getAsJsonObject();
      Address a = at(s.get("Address").getAsLong());
      label(a, "StringLiteral_" + idx++);
      setEOLComment(a, s.get("Value").getAsString());
    }

    for (JsonElement e : root.getAsJsonArray("ScriptMetadata")) {
      JsonObject s = e.getAsJsonObject();
      Address a = at(s.get("Address").getAsLong());
      label(a, s.get("Name").getAsString());
      setEOLComment(a, s.get("Name").getAsString());
    }
    for (JsonElement e : root.getAsJsonArray("ScriptMetadataMethod")) {
      JsonObject s = e.getAsJsonObject();
      Address a = at(s.get("Address").getAsLong());
      label(a, s.get("Name").getAsString());
      setEOLComment(a, s.get("Name").getAsString());
    }

    int ok = 0, bad = 0;
    for (JsonElement e : methods) {
      JsonObject m = e.getAsJsonObject();
      Address a = at(m.get("Address").getAsLong());
      String sig = m.get("Signature").getAsString();
      if (sig.endsWith(";")) sig = sig.substring(0, sig.length() - 1);
      try {
        FunctionDefinitionDataType def = CParserUtils.parseSignature((ghidra.app.services.DataTypeManagerService) null,
                                                                     currentProgram, sig, false);
        if (def == null) { ++bad; continue; }
        def.setName(m.get("Name").getAsString().replace(' ', '-').replaceAll("[^A-Za-z0-9_$.<>,-]", "_"));
        new ApplyFunctionSignatureCmd(a, def, SourceType.USER_DEFINED, false, true).applyTo(currentProgram, monitor);
        ++ok;
      } catch (Exception ex) {
        ++bad;
      }
    }
    println("signatures applied: " + ok + ", failed: " + bad);
  }
}
