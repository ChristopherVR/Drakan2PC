// Creates functions at jal targets / data-pointer targets / prologue-like gap starts missed by auto-analysis.
// @category Drakan
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.MipsDisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.util.*;

public class FixFunctions extends GhidraScript {
    Memory mem; FunctionManager fm; Listing lst; long ts, te;

    boolean inText(long a) { return a >= ts && a < te && (a & 3) == 0; }

    boolean make(long a, String why) throws Exception {
        Address ad = toAddr(a);
        if (fm.getFunctionContaining(ad) != null) return false;
        if (lst.getInstructionAt(ad) == null) {
            disassemble(ad);
            if (lst.getInstructionAt(ad) == null) return false;
        }
        Function f = createFunction(ad, null);
        return f != null;
    }

    @Override
    public void run() throws Exception {
        mem = currentProgram.getMemory(); fm = currentProgram.getFunctionManager(); lst = currentProgram.getListing();
        MemoryBlock text = mem.getBlock(".text");
        ts = text.getStart().getOffset(); te = text.getEnd().getOffset() + 1;
        int total = 0;
        for (int pass = 0; pass < 30; pass++) {
            int n = 0, nj = 0, nd = 0, np = 0;
            // 1. jal targets (raw scan, only where the word is an instruction or undefined)
            TreeSet<Long> jt = new TreeSet<>();
            for (long a = ts; a < te; a += 4) {
                int w = mem.getInt(toAddr(a));
                if ((w >>> 26) == 3) {
                    long t = ((a + 4) & 0xF0000000L) | ((w & 0x3FFFFFFL) << 2);
                    if (inText(t)) {
                        Data d = lst.getDefinedDataAt(toAddr(a));
                        if (d == null) jt.add(t);
                    }
                }
            }
            for (long t : jt) if (make(t, "jal")) nj++;
            // 2. data pointers into text (vtables, callbacks)
            TreeSet<Long> dt = new TreeSet<>();
            for (MemoryBlock b : mem.getBlocks()) {
                if (b.isExecute() || !b.isInitialized() || !b.isLoaded() || b.getStart().getOffset() < 0x100000) continue; println("scan " + b.getName());
                for (long a = b.getStart().getOffset(); a + 4 <= b.getEnd().getOffset() + 1; a += 4) {
                    long v; try { v = mem.getInt(toAddr(a)) & 0xFFFFFFFFL; } catch (MemoryAccessException e) { continue; }
                    if (inText(v) && fm.getFunctionContaining(toAddr(v)) == null) dt.add(v);
                }
            }
            for (long t : dt) {
                // require a plausible entry: addiu sp,sp,-N or an existing instruction flow
                int w = mem.getInt(toAddr(t));
                boolean pro = ((w >>> 16) == 0x27bd && (w & 0x8000) != 0) || ((w >>> 16) == 0x67bd && (w & 0x8000) != 0);
                if (pro && make(t, "data")) nd++;
            }
            // 3. gap starts: right after a function end, prologue-like
            List<Long> starts = new ArrayList<>();
            long cur = ts;
            List<long[]> rs = new ArrayList<>();
            for (Function f : fm.getFunctions(true)) {
                long s = f.getEntryPoint().getOffset();
                if (s < ts || s >= te) continue;
                for (ghidra.program.model.address.AddressRange r : f.getBody().getAddressRanges())
                    rs.add(new long[]{r.getMinAddress().getOffset(), r.getMaxAddress().getOffset() + 1});
            }
            rs.sort((x, y) -> Long.compare(x[0], y[0]));
            for (long[] r : rs) {
                if (r[0] > cur) starts.add(cur);
                cur = Math.max(cur, r[1]);
            }
            if (cur < te) starts.add(cur);
            for (long s : starts) {
                // skip padding (nops)
                long a = s;
                while (a < te && mem.getInt(toAddr(a)) == 0) a += 4;
                if (a >= te) continue;
                int w = mem.getInt(toAddr(a));
                // gap starting right after code and containing a 'jr ra' before the next function: treat as function
                boolean hasRet = false;
                Function nf = getFunctionAfter(toAddr(a));
                long lim = nf != null ? nf.getEntryPoint().getOffset() : te;
                for (long x = a; x < lim; x += 4) if (mem.getInt(toAddr(x)) == 0x03e00008) { hasRet = true; break; }
                if (hasRet && make(a, "gap")) np++;
            }
            n = nj + nd + np;
            println("pass " + pass + ": jal=" + nj + " data=" + nd + " gapprologue=" + np);
            total += n;
            if (n == 0) break;
        }
        println("TOTAL created " + total + "; functions now " + fm.getFunctionCount());
    }
}
