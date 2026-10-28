// Decompile ALL functions into one text file per 256-function chunk. Args: outdir
// @category Drakan
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class InputDecompAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]); out.mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter w = null; int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long a = f.getEntryPoint().getOffset();
            if (a < 0x100000 || a >= 0x4b8000) continue;
            if (n % 500 == 0) { if (w != null) w.close(); w = new PrintWriter(new File(out, String.format("all_%08x.c", a))); }
            n++;
            DecompileResults r = di.decompileFunction(f, 30, monitor);
            w.println("//// FUNC " + f.getName() + " @ " + f.getEntryPoint());
            w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// FAILED");
        }
        if (w != null) w.close();
    }
}
