// Apply names from a csv (addr,name,...) . Args: csvfile
// @category Drakan
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class InputRename extends GhidraScript {
    @Override
    public void run() throws Exception {
        try (BufferedReader br = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String l; br.readLine();
            while ((l = br.readLine()) != null) {
                String[] p = l.split(",", 4);
                if (p.length < 2) continue;
                Address ad = toAddr(Long.parseLong(p[0].replace("0x", ""), 16));
                Function f = currentProgram.getFunctionManager().getFunctionAt(ad);
                if (f != null) f.setName(p[1], SourceType.USER_DEFINED);
                else createLabel(ad, p[1], true);
            }
        }
    }
}
