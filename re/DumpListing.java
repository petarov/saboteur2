// Dumps the SAB2.COM code area as an assembly listing, plus stats and per-procedure fingerprints.
// Import SAB2.COM as a raw binary at 1000:0100 (see NOTES.md / BUILDING.md).
// args: <out_listing_path> <out_stats_path> <out_fingerprint_path>
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.math.BigInteger;
import java.util.*;

public class DumpListing extends GhidraScript {
    static final long CS_LIN = 0x10000L;     // CS = para 1000h (load segment of the raw import)
    static final long CODE_START = 0x0100L;  // entry point, first byte of the .COM image
    static final long CODE_END = 0x2E60L;    // last code byte (RET); DS:0000 = CS:2E60
    static final long DS_PARA = 0x12E6L;     // DS = ES = CS + (2E61h >> 4), set by the entry code
    static final long DS_LIN = DS_PARA << 4;

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Memory mem = currentProgram.getMemory();
        Listing lst = currentProgram.getListing();
        Address codeStart = sp.getAddress(CS_LIN + CODE_START);
        Address codeEnd = sp.getAddress(CS_LIN + CODE_END);

        // Split off the data area (DS:0001 onwards) and mark it non-executable.
        MemoryBlock all = mem.getBlock(codeStart);
        if (all.getEnd().compareTo(codeEnd) > 0) {
            mem.split(all, codeEnd.next());
            mem.getBlock(codeEnd.next()).setExecute(false);
            mem.getBlock(codeEnd.next()).setName("data");
        }
        mem.getBlock(codeStart).setName("code");

        // Ghidra's jump-table recovery (decompiler switch analysis, and constant propagation
        // at the JMP BX sites) reads the DS tables past their real length and disassembles data
        // as code. The tables and continuations are seeded below instead.
        setAnalysisOption(currentProgram, "Decompiler Switch Analysis", "false");
        setAnalysisOption(currentProgram, "x86 Constant Reference Analyzer", "false");

        // Without this, Ghidra resolves [xxxx] against CS and turns code bytes into data.
        Register ds = currentProgram.getRegister("DS");
        currentProgram.getProgramContext().setValue(ds, codeStart, codeEnd, BigInteger.valueOf(DS_PARA));

        // Script-interpreter opcode handlers: word table at DS:1BE8, indexed by opcode (1..105).
        // Entry 0 (FF30h) is not a code address; FFh ends a script. Also used with base
        // DS:1BEA (index + 1) by the 22-record dispatch at CS:071E.
        for (int i = 1; i <= 105; i++)
            seedFromTable(sp, mem, lst, 0x1BE8, i, "script opcode " + i + " (DS:1BE8)");
        // Dispatch at CS:221A: word table at DS:6F5D, indexed by [SI+0Bh] (0..12).
        for (int i = 0; i <= 12; i++)
            seedFromTable(sp, mem, lst, 0x6F5D, i, "case " + i + " of JMP BX at CS:221A (DS:6F5D)");
        // Entry, code reached only through the interrupt vector table, and two starts Ghidra
        // folds into the preceding procedure: CS:0196 follows the DOS exit (INT 21h AH=4Ch)
        // and CS:049A is the head of the frame loop.
        long[] seeds = { 0x0100, 0x0196, 0x049A, 0x0600, 0x2D56 };
        String[] seedNames = { "entry", "startup", "game_loop", "int1c_tick_isr", "int1c_sound_isr" };
        for (int i = 0; i < seeds.length; i++) {
            Address t = sp.getAddress(CS_LIN + seeds[i]);
            disassembleAt(t);
            createFunction(t, seedNames[i]);
        }
        seedContinuations(sp, mem, lst);
        // Code nothing jumps to. Disassembled so the listing has no gaps; marked in a comment.
        String[][] dead = {
            {"0463", "unreachable: JMP at CS:0461 skips it (key-stepping loop, [I] leftover debug code)"},
            {"2B18", "no reference found: second entry into the sound sequencer, SI = [7894h]"},
        };
        for (String[] dd : dead) {
            Address t = sp.getAddress(CS_LIN + Long.parseLong(dd[0], 16));
            disassembleAt(t);
            addComment(lst, t, dd[1]);
        }
        analyzeAll(currentProgram);
        // The data block is not protected by being non-executable; fail if anything got in.
        Instruction stray = lst.getInstructionAfter(codeEnd);
        if (stray != null)
            throw new IllegalStateException("instruction in the data area at " + stray.getAddress());

        // Names inferred from the code (DOS/BIOS calls, data they touch, call sites).
        String[][] names = {
            {"0100", "entry"}, {"012D", "save_scores_exit"}, {"028E", "start_mission"},
            {"04B7", "print_text"}, {"054A", "draw_char_cga"}, {"05BB", "wait_key"}, {"05C6", "hide_cursor"},
            {"05CD", "install_int1c_tick"}, {"05F2", "restore_int1c_tick"}, {"0620", "wait_ticks_2"},
            {"062D", "score_table_encode"}, {"0640", "score_table_decode_check"}, {"0660", "room_setup"},
            {"107C", "rand_step"}, {"10A4", "read_keyboard"}, {"1162", "player_update"},
            {"2772", "toggle_sound"}, {"2843", "high_score_insert"}, {"2A13", "get_key"},
            {"2A18", "flush_keyboard"}, {"2AD8", "sound_seq_play"}, {"2DD0", "speaker_noise"},
        };
        for (String[] n : names) {
            Function fn = getFunctionAt(sp.getAddress(CS_LIN + Long.parseLong(n[0], 16)));
            if (fn == null) throw new IllegalStateException("no function at CS:" + n[0]);
            fn.setName(n[1], SourceType.USER_DEFINED);
        }

        int prologues = 0, retf = 0, ret = 0, iret = 0, calls = 0;
        Map<String, Integer> ints = new TreeMap<>();
        Map<String, Integer> ports = new TreeMap<>();
        List<String> undef = new ArrayList<>();

        try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
            w.println("; SAB2.COM (Saboteur II, Durell 1987) - Ghidra " + getGhidraVersion() + " listing of the code area");
            w.println("; addresses are CS:offset (CS = PSP = para 1000h in this import); DS = ES = para 12E6h (CS + 2E6h)");
            w.println("; data refs [xxxx] are DS-relative: file offset = xxxx + 2D60h; code: file offset = CS offset - 100h");
            w.println();
            Address cur = codeStart;
            Instruction prev = null;
            while (cur != null && cur.compareTo(codeEnd) <= 0) {
                Function f = getFunctionAt(cur);
                if (f != null) {
                    w.println();
                    w.println(";=========================================================");
                    w.printf("%s proc   ; CS:%04X  linear %s%n", f.getName(), cs(cur), cur);
                    w.println(";=========================================================");
                }
                CodeUnit cu = lst.getCodeUnitAt(cur);
                if (cu instanceof Instruction) {
                    Instruction ins = (Instruction) cu;
                    String pre = ins.getComment(CodeUnit.PRE_COMMENT);
                    if (pre != null) w.println("        ; " + pre);
                    Symbol lbl = getSymbolAt(cur);
                    if (lbl != null && f == null && lbl.getSymbolType() == SymbolType.LABEL)
                        w.println(lbl.getName() + ":");
                    String tail = "";
                    for (Address ref : ins.getFlows()) {
                        Function tf = getFunctionAt(ref);
                        if (tf != null && ins.getFlowType().isCall())
                            tail = "   ; " + tf.getName();
                        else if (ref.getOffset() >= CS_LIN + CODE_START && ref.getOffset() <= CS_LIN + CODE_END)
                            tail = String.format("   ; CS:%04X", cs(ref));
                    }
                    w.printf("%04X  %-14s %s%s%n", cs(cur), hex(ins.getBytes()), fixText(ins), tail);
                    String m = ins.getMnemonicString();
                    if (m.equals("INT")) ints.merge(ins.getDefaultOperandRepresentation(0), 1, Integer::sum);
                    if (m.equals("IN") || m.equals("OUT")) ports.merge(m + " " + ins.toString().substring(m.length()).trim(), 1, Integer::sum);
                    if (m.equals("RETF")) retf++;
                    if (m.equals("RET")) ret++;
                    if (m.equals("IRET")) iret++;
                    if (m.equals("CALL")) calls++;
                    if (prev != null && prev.toString().equals("PUSH BP") && ins.toString().equals("MOV BP,SP")) prologues++;
                    prev = ins;
                    cur = ins.getMaxAddress().next();
                } else {
                    // Gather a run of non-instruction bytes up to the next instruction or function.
                    Address runStart = cur;
                    StringBuilder sb = new StringBuilder();
                    int n = 0;
                    while (cur != null && cur.compareTo(codeEnd) <= 0 && !(lst.getCodeUnitAt(cur) instanceof Instruction)
                            && (n == 0 || getFunctionAt(cur) == null)) {
                        CodeUnit c2 = lst.getCodeUnitAt(cur);
                        int len = c2 == null ? 1 : c2.getLength();
                        for (int i = 0; i < len; i++) {
                            if (n % 16 == 0) {
                                if (n > 0) sb.append('\n');
                                sb.append(String.format("%04X  db ", cs(cur.add(i))));
                            } else sb.append(',');
                            sb.append(String.format("%02Xh", mem.getByte(cur.add(i)) & 0xff));
                            n++;
                        }
                        cur = cur.add(len);
                    }
                    undef.add(String.format("CS:%04X-%04X (%d bytes)", cs(runStart), cs(runStart) + n - 1, n));
                    w.println("        ; ---- " + n + " non-code bytes ----");
                    w.println(sb);
                    prev = null;
                }
            }
        }

        int nf = 0;
        try (PrintWriter fp = new PrintWriter(new FileWriter(a[2]))) {
            fp.println("; CS   name                       ins   calls callers tags");
            fp.println("; tags: bios-video=INT 10h, kbd=INT 16h, dos=INT 21h, clock=INT 1Ah, ivt=ES:0070h/0072h");
            fp.println(";       (INT 1Ch vector) access, cga-vram=immediate B800h, speaker=port 61h, iret=ends in IRET");
            fp.println("; callers: references to the entry that Ghidra types as calls (a JMP to an entry can count)");
            FunctionIterator fi = currentProgram.getFunctionManager().getFunctions(true);
            while (fi.hasNext()) {
                Function f = fi.next();
                long off = f.getEntryPoint().getOffset() - CS_LIN;
                if (off < CODE_START || off > CODE_END) continue;
                nf++;
                int nIns = 0, nCalls = 0;
                Set<String> tags = new TreeSet<>();
                InstructionIterator ii = lst.getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    nIns++;
                    String m = ins.getMnemonicString();
                    String t = ins.toString();
                    if (m.equals("CALL")) nCalls++;
                    if (m.equals("IRET")) tags.add("iret");
                    if (m.equals("INT")) {
                        String v = ins.getDefaultOperandRepresentation(0);
                        if (v.equals("0x10")) tags.add("bios-video");
                        if (v.equals("0x16")) tags.add("kbd");
                        if (v.equals("0x21")) tags.add("dos");
                        if (v.equals("0x1a")) tags.add("clock");
                    }
                    if ((m.equals("IN") || m.equals("OUT")) && t.contains("0x61")) tags.add("speaker");
                    if (t.contains("ES:[0x70]") || t.contains("ES:[0x72]")) tags.add("ivt");
                    for (int op = 0; op < ins.getNumOperands(); op++)
                        for (Object o : ins.getOpObjects(op))
                            if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == 0xB800) tags.add("cga-vram");
                }
                int callers = 0;
                for (Reference r : getReferencesTo(f.getEntryPoint()))
                    if (r.getReferenceType().isCall()) callers++;
                fp.printf("%04X %-26s ins=%-4d calls=%-3d callers=%-3d %s%n", off, f.getName(), nIns, nCalls, callers,
                    String.join(",", tags));
            }
        }

        try (PrintWriter s = new PrintWriter(new FileWriter(a[1]))) {
            s.println("functions: " + nf);
            s.println("PUSH BP/MOV BP,SP prologues: " + prologues);
            s.println("RET: " + ret + "  RETF: " + retf + "  IRET: " + iret + "  CALL: " + calls);
            s.println("INT usage: " + ints);
            s.println("IN/OUT usage: " + ports);
            s.println("non-code runs in code area:");
            for (String u : undef) s.println("  " + u);
        }
    }

    // Code addresses kept in two variables and reached by JMP BX:
    //   [3D38h]: where the script-opcode handlers return to (MOV word ptr [3D38h],imm).
    //   [1BBAh]: the player state handler, jumped to at CS:11A9. Set by MOV word ptr [1BBAh],imm,
    //            or by MOV BX,imm followed within 4 instructions by MOV [1BBAh],BX or a JMP to
    //            one of the shared tails CS:1206/1209/120D that end in that store.
    // Newly disassembled code can contain further such stores, so repeat until none are new.
    private void seedContinuations(AddressSpace sp, Memory mem, Listing lst) throws Exception {
        Address lo = sp.getAddress(CS_LIN + CODE_START), hi = sp.getAddress(CS_LIN + CODE_END);
        Set<Long> tails = new HashSet<>(Arrays.asList(0x1206L, 0x1209L, 0x120DL));
        boolean added = true;
        while (added) {
            added = false;
            List<long[]> found = new ArrayList<>();  // {target, variable}
            InstructionIterator it = lst.getInstructions(new AddressSet(lo, hi), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                byte[] b = ins.getBytes();
                int op0 = b[0] & 0xff;
                if (op0 == 0xC7 && b.length == 6 && (b[1] & 0xff) == 0x06) {
                    int var = (b[2] & 0xff) | (b[3] & 0xff) << 8;
                    if (var == 0x3D38 || var == 0x1BBA)
                        found.add(new long[] { (b[4] & 0xff) | (b[5] & 0xff) << 8, var });
                } else if (op0 == 0xBB && b.length == 3) {
                    long imm = (b[1] & 0xff) | (b[2] & 0xff) << 8;
                    Instruction n = ins;
                    for (int k = 0; k < 4 && n != null; k++) {
                        Address ft = n.getFallThrough();
                        Address[] fl = n.getFlows();
                        if (k > 0) {
                            byte[] nb = n.getBytes();
                            boolean store = nb.length == 4 && (nb[0] & 0xff) == 0x89 && (nb[1] & 0xff) == 0x1E
                                && (nb[2] & 0xff) == 0xBA && (nb[3] & 0xff) == 0x1B;
                            boolean tail = n.getFlowType().isJump() && fl.length == 1 && tails.contains(cs(fl[0]));
                            if (store || tail) {
                                found.add(new long[] { imm, 0x1BBA });
                                break;
                            }
                        }
                        n = ft == null ? null : lst.getInstructionAt(ft);
                    }
                }
            }
            for (long[] f : found) {
                if (f[0] < CODE_START || f[0] > CODE_END)
                    throw new IllegalStateException(String.format("continuation %04X outside the code", f[0]));
                Address t = sp.getAddress(CS_LIN + f[0]);
                String c = String.format("continuation stored in [%04Xh]", f[1]);
                String old = lst.getComment(CodeUnit.PRE_COMMENT, t);
                if (old != null && old.contains(c)) continue;
                if (lst.getInstructionAt(t) == null) {
                    disassembleAt(t);
                    added = true;
                }
                addComment(lst, t, c);
            }
        }
    }

    private void seedFromTable(AddressSpace sp, Memory mem, Listing lst, long dsTable, int idx, String comment)
            throws Exception {
        int off = mem.getShort(sp.getAddress(DS_LIN + dsTable + idx * 2L)) & 0xffff;
        if (off < CODE_START || off > CODE_END)
            throw new IllegalStateException(String.format("DS:%04X[%d] = %04X is outside the code", dsTable, idx, off));
        Address t = sp.getAddress(CS_LIN + off);
        disassembleAt(t);
        addComment(lst, t, comment);
    }

    private void disassembleAt(Address t) {
        DisassembleCommand cmd = new DisassembleCommand(t, null, true);
        cmd.enableCodeAnalysis(false);
        cmd.applyTo(currentProgram, monitor);
    }

    private static void addComment(Listing lst, Address t, String c) {
        String old = lst.getComment(CodeUnit.PRE_COMMENT, t);
        lst.setComment(t, CodeUnit.PRE_COMMENT, old == null ? c : old + "\n        ; " + c);
    }

    private static long cs(Address x) {
        return x.getOffset() - CS_LIN;
    }

    // Ghidra 12.1.4 renders opcode 8E /r (MOV Sreg,r/m16) with operands reversed, e.g. 8E C0 as
    // "MOV AX,ES"; swap them back so the listing reads MOV ES,AX.
    private String fixText(Instruction ins) throws Exception {
        String t = ins.toString();
        byte[] b = ins.getBytes();
        int i = 0;
        while (i < b.length && (b[i] == 0x26 || b[i] == 0x2E || b[i] == 0x36 || b[i] == 0x3E)) i++;
        if (i < b.length && (b[i] & 0xff) == 0x8E && t.startsWith("MOV ")) {
            String ops = t.substring(4);
            int comma = ops.indexOf(',');
            String dst = ops.substring(0, comma), src = ops.substring(comma + 1);
            if (dst.matches("[A-D]X|SI|DI|BP|SP") || dst.contains("[")) {
                if (src.matches("[CDES]S")) t = "MOV " + src + "," + dst;
            }
        }
        return t;
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(String.format("%02X", x & 0xff));
        return sb.toString();
    }
}
