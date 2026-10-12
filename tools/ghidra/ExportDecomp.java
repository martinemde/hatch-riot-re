// Headless: decompile functions to one C file.
// args: OUT.c [ADDR ...]   (no addresses = every function)
// e.g. analyzeHeadless PROJ_DIR riot -process riot_7_1_644.elf -noanalysis -scriptPath tools/ghidra \
//        -postScript ExportDecomp.java /tmp/out.c 401c749c 401c7018
//@category Export
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class ExportDecomp extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    List<Function> funcs = new ArrayList<>();
    if (args.length > 1) {
      for (int i = 1; i < args.length; i++) {
        Function f = getFunctionContaining(toAddr(Long.parseUnsignedLong(args[i].replace("0x", ""), 16)));
        if (f == null)
          f = createFunction(toAddr(Long.parseUnsignedLong(args[i].replace("0x", ""), 16)), null);
        if (f != null)
          funcs.add(f);
        else
          println("no function at " + args[i]);
      }
    } else {
      currentProgram.getFunctionManager().getFunctions(true).forEach(funcs::add);
    }
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    int n = 0;
    try (PrintWriter out = new PrintWriter(args[0])) {
      for (Function f : funcs) {
        if (monitor.isCancelled())
          break;
        if (f.isExternal() || f.getBody().getNumAddresses() == 0)
          continue;
        DecompileResults r = di.decompileFunction(f, 60, monitor);
        out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
        out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
        if (++n % 500 == 0)
          println("decompiled " + n + "/" + funcs.size());
      }
    }
    di.dispose();
    println("wrote " + n + " functions to " + args[0]);
  }
}
