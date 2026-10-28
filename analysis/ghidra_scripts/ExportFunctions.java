// Exports functions.csv, gaps.csv, jumptables.csv.  Args: <outdir>
// @category Drakan
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.lang.*;
import java.io.*;
import java.util.*;

public class ExportFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "analysis";
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) text = currentProgram.getMemory().getBlock("text");
        long ts = text.getStart().getOffset(), te = text.getEnd().getOffset() + 1;
        println("text " + Long.toHexString(ts) + ".." + Long.toHexString(te));
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        List<long[]> ranges = new ArrayList<>();
        try (PrintWriter w = new PrintWriter(new File(out, "functions.csv"))) {
            w.println("start_addr_hex,end_addr_hex,size,name,num_callers,is_thunk");
            for (Function f : fm.getFunctions(true)) {
                long s = f.getEntryPoint().getOffset();
                if (s < ts || s >= te) continue;
                // union of body ranges; use the range containing entry for start/end
                long minS = s, maxE = s;
                for (AddressRange r : f.getBody().getAddressRanges()) {
                    long a = r.getMinAddress().getOffset(), b = r.getMaxAddress().getOffset() + 1;
                    ranges.add(new long[]{a, b});
                    if (a <= s && s < b) { minS = a; maxE = b; }
                }
                // callers: distinct call-type references to the entry
                Set<Long> callers = new HashSet<>();
                for (Reference r : rm.getReferencesTo(f.getEntryPoint()))
                    if (r.getReferenceType().isCall()) callers.add(r.getFromAddress().getOffset());
                w.printf("%08x,%08x,%d,%s,%d,%d%n", minS, maxE, maxE - minS, f.getName(), callers.size(), f.isThunk() ? 1 : 0);
            }
        }
        ranges.sort((x, y) -> Long.compare(x[0], y[0]));
        long covered = 0, cur = ts; int gaps = 0, nfun = 0;
        try (PrintWriter w = new PrintWriter(new File(out, "gaps.csv"))) {
            w.println("start_addr_hex,end_addr_hex,size,all_zero_or_nop");
            Memory mem = currentProgram.getMemory();
            for (long[] r : ranges) {
                if (r[1] <= cur) continue;
                if (r[0] > cur) { gaps += emit(w, mem, cur, r[0]); }
                long a = Math.max(r[0], cur);
                covered += r[1] - a; cur = r[1];
            }
            if (cur < te) gaps += emit(w, mem, cur, te);
        }
        println(String.format("COVERAGE %.3f%% of %d bytes; big gaps=%d", 100.0 * covered / (te - ts), te - ts, gaps));

        // jump tables: switch-style indirect jumps (jr non-ra) with computed-jump refs
        int jt = 0;
        try (PrintWriter w = new PrintWriter(new File(out, "jumptables.csv"))) {
            w.println("jr_addr_hex,table_addr_hex,num_targets,targets_hex");
            Listing l = currentProgram.getListing();
            for (Function f : fm.getFunctions(true)) {
                long s = f.getEntryPoint().getOffset();
                if (s < ts || s >= te) continue;
                InstructionIterator it = l.getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction in = it.next();
                    if (!in.getMnemonicString().equals("jr")) continue;
                    if (in.getFlowType().isComputed() && in.getFlowType().isJump()) {
                        List<Long> tg = new ArrayList<>();
                        for (Reference r : in.getReferencesFrom())
                            if (r.getReferenceType().isComputed() || r.getReferenceType().isJump())
                                tg.add(r.getToAddress().getOffset());
                        if (tg.isEmpty()) continue;
                        Collections.sort(tg);
                        long table = findTable(in, tg);
                        StringBuilder sb = new StringBuilder();
                        for (long t : tg) sb.append(sb.length() > 0 ? ";" : "").append(String.format("%08x", t));
                        w.printf("%08x,%s,%d,%s%n", in.getAddress().getOffset(), table < 0 ? "" : String.format("%08x", table), tg.size(), sb);
                        jt++;
                    }
                }
            }
        }
        println("JUMPTABLES " + jt);
    }

    // best effort: look at data refs in the ~12 preceding instructions for an address in .rodata/.data
    long findTable(Instruction jr, List<Long> tg) {
        Instruction p = jr.getPrevious();
        for (int i = 0; i < 14 && p != null; i++, p = p.getPrevious()) {
            for (Reference r : p.getReferencesFrom()) {
                if (r.getReferenceType().isData()) {
                    MemoryBlock b = currentProgram.getMemory().getBlock(r.getToAddress());
                    if (b != null && !b.isExecute()) return r.getToAddress().getOffset();
                }
            }
        }
        return -1;
    }

    int emit(PrintWriter w, Memory mem, long a, long b) throws Exception {
        if (b - a <= 64) return 0;
        boolean zero = true;
        for (long x = a; x < b && zero; x += 4) {
            int v = mem.getInt(toAddr(x));
            if (v != 0) zero = false;
        }
        w.printf("%08x,%08x,%d,%d%n", a, b, b - a, zero ? 1 : 0);
        return 1;
    }
}
