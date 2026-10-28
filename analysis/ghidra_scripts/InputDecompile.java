// Decompile functions to C. Args: outdir addrhex[,name] ...   (one file per function; also callee list)
// @category Drakan
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class InputDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (int i = 1; i < a.length; i++) {
            String s = a[i].replace("0x", "");
            long v = Long.parseLong(s.split(",")[0], 16);
            Address ad = toAddr(v);
            Function f = currentProgram.getFunctionManager().getFunctionAt(ad);
            if (f == null) f = currentProgram.getFunctionManager().getFunctionContaining(ad);
            if (f == null) { println("no function at " + ad); continue; }
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            try (PrintWriter w = new PrintWriter(new File(out, String.format("%08x_%s.c", f.getEntryPoint().getOffset(), f.getName())))) {
                w.println("// " + f.getName() + " @ " + f.getEntryPoint() + " size " + f.getBody().getNumAddresses());
                Set<String> callees = new TreeSet<>();
                for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getEntryPoint() + " " + c.getName());
                w.println("// callees: " + callees);
                Set<String> callers = new TreeSet<>();
                for (Function c : f.getCallingFunctions(monitor)) callers.add(c.getEntryPoint() + " " + c.getName());
                w.println("// callers: " + callers);
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// DECOMPILE FAILED " + r.getErrorMessage());
            }
        }
    }
}
