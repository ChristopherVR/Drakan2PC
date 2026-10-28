// Print function-level xrefs to given addresses (data or code). Args: outfile addr1 addr2 ...
// @category Drakan
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class InputXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new FileWriter(a[0], true))) {
            for (int i = 1; i < a.length; i++) {
                Address ad = toAddr(Long.parseLong(a[i].replace("0x", ""), 16));
                w.println("== " + ad);
                // refs to address itself; for data also scan lui/addiu pairs via Ghidra refs
                for (Reference r : currentProgram.getReferenceManager().getReferencesTo(ad)) {
                    Function f = currentProgram.getFunctionManager().getFunctionContaining(r.getFromAddress());
                    w.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (f == null ? "?" : f.getName()));
                }
            }
        }
    }
}
