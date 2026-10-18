#!/usr/bin/env python3
"""Translate the Ghidra listing of SAB2.COM (re/SAB2.asm) into x86-64 nasm.

Usage: asm2nasm.py <re/SAB2.asm> <out.asm>

Every instruction gets a label L_xxxx (its CS offset), so branches, shared
tails and fallthrough between procedures keep working. The register and
segment model is described in src/runtime.inc. Instructions the translator
does not handle become TRAP <cs>, which aborts at runtime with the offset, so
the output always assembles.

JMP BX and the INT 1Ch dispatch (game.asm) go through cs_labels, a table with
one entry per CS offset: the instruction's label, or 0 where no instruction
starts.
"""
import re
import sys

CODE_END = 0x2E60      # last code byte; cs_labels covers CS:0000..CODE_END

# CS offset -> nanoseconds of 8088 time to spend before that instruction.
# Only for loops whose sound depends on CPU speed: they toggle the speaker
# from software. Values are 8088 cycle counts at 4.77 MHz (209.6 ns/cycle)
# from the datasheet timings; refine by ear.
CPU_PACE_NS = {
    0x2C7C: 104800,   # tone loop head: one pass without the LOOP $ (~500 cycles)
    0x2CBC: 3563,     # LOOP $ (17 cycles), the tone period delay
    0x2DDC: 18860,    # speaker_noise: one speaker toggle (~90 cycles)
    0x2DE5: 4402,     # speaker_noise: SHR/DEC/JNZ shift loop (21 cycles)
    0x2DED: 5240,     # speaker_noise: DEC/NOP/NOP/JNZ delay loop (25 cycles)
}

# CS offset -> lines emitted before that instruction: the port's changes.
MARKERS = {
    # wait_ticks_2 loop head: sleep until the next INT 1Ch tick is due and run
    # the installed handler, instead of spinning until it fires.
    0x0620: ['TIMER_POLL timer_poll_wait'],
    # CPU speed calibration (CS:0270-028A): counts a delay loop until the next
    # tick. A host CPU would wrap the 16-bit count, so take the 4.77 MHz
    # result (below 600h: add 8 to [79A9h]), which matches CPU_PACE_NS.
    0x0270: ['jmp L_0286'],
    # start_mission, before PAY is reset: drop pay and time cheat presses
    # made outside a mission.
    0x039D: ['mov byte [cheat_pay], 0', 'mov byte [cheat_time], 0'],
    # game_loop head: apply the pay cheat (platform.c, Ctrl-P). CS:1F6B adds
    # CX to PAY (6 digits at DS:195E, shown with a fixed trailing 0, so one
    # unit is $10) and reprints it. Registers, ES and print_text's position/colour are kept.
    0x049A: ['cmp byte [cheat_pay], 0', 'je .no_pay',
             'push rax', 'push rbx', 'push rcx', 'push rdx', 'push rsi', 'push rdi', 'push rbp',
             'PUSH_SEG es', 'movzx r14d, word [r12 + 0x1BA2]', 'push r14',
             'movzx r14d, byte [r12 + 0x1BC4]', 'push r14',
             'xor eax, eax', 'xchg al, [cheat_pay]', 'imul ecx, eax, 10', 'call L_1F6B',
             'pop r14', 'mov [r12 + 0x1BC4], r14b',
             'pop r14', 'mov [r12 + 0x1BA2], r14w', 'POP_SEG es',
             'pop rbp', 'pop rdi', 'pop rsi', 'pop rdx', 'pop rcx', 'pop rbx', 'pop rax',
             '.no_pay:'],
    # game_loop, before read_keyboard: apply the time cheat (platform.c,
    # Ctrl-J). The timer is 3 ASCII digits at DS:1BE6 (counted down by
    # CS:0FA0), printed by the text at DS:1BE1. Add 100 per press, at most
    # 999, and reprint it. Registers, ES and print_text's position/colour are
    # kept.
    0x049D: ['cmp byte [cheat_time], 0', 'je .no_time',
             'push rax', 'push rbx', 'push rcx', 'push rdx', 'push rsi', 'push rdi', 'push rbp',
             'PUSH_SEG es', 'movzx r14d, word [r12 + 0x1BA2]', 'push r14',
             'movzx r14d, byte [r12 + 0x1BC4]', 'push r14',
             'movzx eax, byte [r12 + 0x1BE6]', 'sub eax, 0x30', 'imul eax, eax, 10',
             'movzx ecx, byte [r12 + 0x1BE7]', 'lea eax, [rax + rcx - 0x30]', 'imul eax, eax, 10',
             'movzx ecx, byte [r12 + 0x1BE8]', 'lea eax, [rax + rcx - 0x30]',
             'xor ecx, ecx', 'xchg cl, [cheat_time]', 'imul ecx, ecx, 100', 'add eax, ecx',
             'mov ecx, 999', 'cmp eax, ecx', 'cmova eax, ecx',
             'mov ecx, 10', 'xor edx, edx', 'div ecx', 'add dl, 0x30', 'mov [r12 + 0x1BE8], dl',
             'xor edx, edx', 'div ecx', 'add dl, 0x30', 'mov [r12 + 0x1BE7], dl',
             'add al, 0x30', 'mov [r12 + 0x1BE6], al',
             'mov si, 0x1BE1', 'call L_04B7',
             'pop r14', 'mov [r12 + 0x1BC4], r14b',
             'pop r14', 'mov [r12 + 0x1BA2], r14w', 'POP_SEG es',
             'pop rbp', 'pop rdi', 'pop rsi', 'pop rdx', 'pop rcx', 'pop rbx', 'pop rax',
             '.no_time:'],
    # read_keyboard, after the control keys: Esc acts as Ctrl-C (03h), which
    # sets [1BD5h] = 3 and so ends the mission like the other end states.
    0x10CA: ['cmp al, 0x1B', 'jne .not_esc', 'mov al, 0x03', '.not_esc:'],
    # read_keyboard, after the buffered keys, before the countdowns: the held
    # control keys (platform.c, Ctrl-K). Each entry at DS:5800 is key, second
    # key, action|flags (80h held, 40h one more countdown before release),
    # countdown. A held entry whose key is down (ctl_down) gets countdown 4,
    # so it never expires; one whose key is up is released at once, as
    # CS:1152 does, unless its first press came this frame (countdown 2 with
    # 40h, or 6 for down), so even the shortest tap is seen for a frame.
    0x112C: ['cmp byte [ctl_hold], 0', 'je .no_hold',
             'push rax', 'push rbx', 'push rcx', 'push rdx',
             'mov al, [ctl_down]', 'lea rbx, [r12 + 0x5800]', 'mov ecx, 5',
             '.hold_next:', 'mov dl, [rbx + 2]', 'test dl, 0x80', 'jz .hold_skip',
             'test al, 1', 'jz .hold_up',
             'cmp byte [rbx + 3], 4', 'jae .hold_skip', 'mov byte [rbx + 3], 4', 'jmp .hold_skip',
             '.hold_up:', 'cmp byte [rbx + 3], 6', 'je .hold_skip',
             'cmp byte [rbx + 3], 2', 'jne .hold_release', 'test dl, 0x40', 'jnz .hold_skip',
             '.hold_release:', 'and byte [rbx + 2], 0x3F', 'mov byte [rbx + 3], 0',
             'not dl', 'and [r12 + 0x5B35], dl',
             '.hold_skip:', 'shr al, 1', 'add rbx, 4', 'dec ecx', 'jnz .hold_next',
             'pop rdx', 'pop rcx', 'pop rbx', 'pop rax',
             '.no_hold:'],
    # Damage (CS:1F89): CS:1F91 takes CX units off the energy bar at
    # [1BC0h]/[1BDBh]; an empty bar sets [1BD5h] = 1 (death). With the
    # invincibility cheat (platform.c, Ctrl-I) return at once. All damage
    # enters here; regeneration (CS:2015) calls CS:1F91 directly and still runs.
    0x1F89: ['cmp byte [cheat_invincible], 0', 'jne L_1F90'],
    # Player hits an enemy (CS:21C8-2206): $20, then [6F98h] counts down the
    # hits left (5, shared by all enemies); at 0, or on a 1-in-8 chance, the
    # enemy goes down (state 5/6) for $100. With the one-hit cheat
    # (platform.c, Ctrl-O) every hit is the last one.
    0x21D3: ['cmp byte [cheat_onehit], 0', 'je .no_onehit',
             'mov byte [r12 + 0x6F98], 1', '.no_onehit:'],
    # Menu, after its text is printed: add the port's version (data.asm) at the
    # top left of the panel and the C SEE CHEATS item. Text position and
    # colour are kept as they were.
    0x2742: ['push rsi', 'movzx r14d, word [r12 + 0x1BA2]', 'push r14',
             'movzx r14d, byte [r12 + 0x1BC4]', 'push r14',
             'mov si, VERSION_TEXT', 'call L_04B7',
             'pop r14', 'mov [r12 + 0x1BC4], r14b',
             'pop r14', 'mov [r12 + 0x1BA2], r14w', 'pop rsi'],
    # Menu, after the key is read (AL, upper case): C shows the port's cheat
    # keys (data.asm) as CS:2897 shows the high scores: clear the panel
    # (CS:2A93) and print, then CS:28CC waits for a key or 100 frames and goes
    # back to the menu.
    0x275A: ['cmp al, 0x43', 'jne .not_cheats', 'call L_2A93',
             'mov si, CHEATS_TEXT', 'call L_04B7', 'jmp L_28CC', '.not_cheats:'],
    # Tone loop of sound_seq_play: [78BCh] is cleared only by the INT 1Ch
    # handler, so run it here when a tick is due (no sleep; the loop is paced).
    0x2D0B: ['TIMER_POLL timer_poll_run'],
    # STI; JMP $: waits forever (also on the original). Sleep between ticks.
    0x2D54: ['TIMER_POLL timer_poll_wait'],
}

REG16 = {'AX': 'ax', 'BX': 'bx', 'CX': 'cx', 'DX': 'dx', 'SI': 'si', 'DI': 'di', 'BP': 'bp', 'SP': 'sp'}
REG8 = {'AL': 'al', 'BL': 'bl', 'CL': 'cl', 'DL': 'dl', 'AH': 'ah', 'BH': 'bh', 'CH': 'ch', 'DH': 'dh'}
HIGH_TO_LOW = {'ah': 'al', 'bh': 'bl', 'ch': 'cl', 'dh': 'dl'}
REG64 = {'AX': 'rax', 'BX': 'rbx', 'CX': 'rcx', 'DX': 'rdx', 'SI': 'rsi', 'DI': 'rdi', 'BP': 'rbp'}
SEG_BASE = {'DS': 'r12', 'ES': 'r13'}

CS_VALUE = '0x1000'    # the code segment value (CS_VALUE in runtime.inc)

# Mnemonics that translate 1:1 once operands are rewritten.
SIMPLE = {
    'MOV', 'ADD', 'SUB', 'CMP', 'AND', 'OR', 'XOR', 'TEST', 'ADC', 'SBB', 'INC', 'DEC',
    'NOT', 'NEG', 'SHL', 'SHR', 'SAR', 'ROL', 'ROR', 'RCL', 'RCR', 'MUL', 'DIV', 'XCHG',
}
JUMPS = {
    'JMP', 'JZ', 'JNZ', 'JC', 'JNC', 'JA', 'JBE', 'JS', 'JNS', 'JL', 'JGE', 'JLE', 'JG',
    'JO', 'JNO', 'JP', 'JNP',
}
# IRET becomes RET: game.asm calls the INT 1Ch handlers with the flags saved.
NOARG = {'CLD': 'cld', 'STD': 'std', 'CLC': 'clc', 'STC': 'stc', 'CMC': 'cmc', 'NOP': 'nop',
         'RET': 'ret', 'PUSHF': 'pushfq', 'POPF': 'popfq', 'CLI': 'nop', 'STI': 'nop',
         'LAHF': 'LAHF16', 'SAHF': 'SAHF16', 'AAA': 'AAA16', 'CBW': 'cbw', 'IRET': 'ret'}
STRING_RE = re.compile(r'(MOVS|STOS|LODS|SCAS|CMPS)([BW])(?:\.(REP|REPNE|REPE))?$')
# INT n -> runtime.inc macro and C handler.
INTS = {0x10: 'BIOS bios_int10', 0x16: 'BIOS_ZF bios_int16', 0x1A: 'BIOS bios_int1a',
        0x21: 'DOS bios_int21'}

INSN_RE = re.compile(r'([0-9A-F]{4})  ([0-9A-F]+) +(\S+)\s*(.*)$')
PROC_RE = re.compile(r'(\S+) proc   ; CS:([0-9A-F]{4})')
IMM_RE = r'-?0x[0-9a-f]+'


class Untranslatable(Exception):
    pass


def target_label(text):
    m = re.fullmatch(r'0x1000:([0-9a-f]+)', text)
    if not m:
        raise Untranslatable('branch target ' + text)
    return 'L_%04X' % int(m.group(1), 16)


def split_operands(text):
    """Split on commas outside brackets."""
    ops, depth, cur = [], 0, ''
    for ch in text:
        if ch == '[':
            depth += 1
        elif ch == ']':
            depth -= 1
        if ch == ',' and depth == 0:
            ops.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        ops.append(cur.strip())
    return ops


class Operand:
    """One translated operand plus the setup lines it needs (address LEA)."""

    def __init__(self, text):
        self.setup = []
        self.is_mem = False
        self.high = None
        t = text.strip()
        size = ''
        m = re.match(r'(byte|word) ptr (.*)$', t)
        if m:
            size, t = m.group(1) + ' ', m.group(2)
        seg, explicit = 'DS', False
        m = re.match(r'(ES|DS|CS|SS):(\[.*\])$', t)
        if m:
            seg, t, explicit = m.group(1), m.group(2), True
        if t.startswith('['):
            self.is_mem = True
            self.text = self._memory(t[1:-1], seg, explicit, size)
        elif t in REG16:
            self.text = REG16[t]
        elif t in REG8:
            self.text = REG8[t]
            if self.text in HIGH_TO_LOW:
                self.high = self.text
        elif re.fullmatch(IMM_RE, t):
            self.text = t
        elif t == 'CS':
            self.text = CS_VALUE
        else:
            raise Untranslatable('operand ' + text)

    def _memory(self, inner, seg, explicit, size):
        terms = [x.strip() for x in inner.split('+')]
        regs, disp = [], 0
        for term in terms:
            if term in ('BX', 'SI', 'DI', 'BP'):
                regs.append(term)
            elif re.fullmatch(IMM_RE, term):
                disp += int(term, 16)
            else:
                raise Untranslatable('address ' + inner)
        if 'BP' in regs and not explicit:
            raise Untranslatable('BP addressing defaults to SS')
        if seg not in SEG_BASE:
            raise Untranslatable(seg + ' segment operand')
        base = SEG_BASE[seg]
        if not regs:
            return '%s[%s + 0x%x]' % (size, base, disp & 0xFFFF)
        # Exact 16-bit effective address, flag-free: lea + movzx into r14.
        expr = ' + '.join(REG64[r] for r in regs)
        self.setup = ['lea r14d, [%s + 0x%x]' % (expr, disp & 0xFFFF), 'movzx r14d, r14w']
        return '%s[%s + r14]' % (size, base)


def translate(mnem, args, sp_slots):
    """Return a list of nasm lines for one listing instruction."""
    args = args.split('   ;')[0].strip()

    if mnem == 'JMP' and args in REG16:
        return ['JMP_CS %s' % REG16[args]]
    if mnem in JUMPS:
        return ['%s %s' % (mnem.lower(), target_label(args))]
    if mnem == 'JCXZ':
        return ['JCXZ16 ' + target_label(args)]
    if mnem == 'CALL':
        return ['call ' + target_label(args)]
    if mnem in ('LOOP', 'LOOPZ', 'LOOPNZ'):
        return ['%s16 %s' % (mnem, target_label(args))]
    if mnem in NOARG and not args:
        return [NOARG[mnem]]
    if mnem == 'XLAT' and args == 'BX':
        return ['XLAT16']

    m = STRING_RE.match(mnem)
    if m:
        op, width, rep = m.groups()
        insn = op.lower() + width.lower()
        if rep:
            insn = rep.lower() + ' ' + insn
        return ['STRINGOP {%s}' % insn]

    if mnem == 'INT':
        n = int(args, 16)
        if n in INTS:
            return [INTS[n]]
        raise Untranslatable('INT ' + args)
    if mnem == 'IN':
        dst, port = split_operands(args)
        if dst != 'AL':
            raise Untranslatable('IN ' + args)
        return ['PORT_IN_DX' if port == 'DX' else 'PORT_IN %s' % port]
    if mnem == 'OUT':
        port, src = split_operands(args)
        if src != 'AL':
            raise Untranslatable('OUT ' + args)
        return ['PORT_OUT_DX' if port == 'DX' else 'PORT_OUT %s' % port]

    if mnem in ('PUSH', 'POP'):
        if args in REG64:
            return ['%s %s' % (mnem.lower(), REG64[args])]
        if args in ('DS', 'ES'):
            return ['%s_SEG %s' % (mnem, args.lower())]
        if mnem == 'PUSH' and args == 'CS':
            return ['push qword ' + CS_VALUE]
        raise Untranslatable(mnem + ' ' + args)

    ops = split_operands(args)
    # MOV [mem],SP / MOV SP,[mem] (sound_seq_play): the host stack pointer is
    # saved to and restored from a qword of its own, one per memory word.
    if 'SP' in ops:
        m = re.fullmatch(r'word ptr \[(0x[0-9a-f]+)\]', ops[1 if ops[0] == 'SP' else 0])
        if mnem != 'MOV' or len(ops) != 2 or not m:
            raise Untranslatable('SP operand')
        slot = 'sp_save_%04X' % int(m.group(1), 16)
        sp_slots.add(slot)
        return ['mov rsp, [%s]' % slot] if ops[0] == 'SP' else ['mov [%s], rsp' % slot]
    if mnem == 'MOV' and len(ops) == 2 and ops[0] in ('DS', 'ES'):
        if ops[1] not in REG16:
            raise Untranslatable('MOV sreg from ' + ops[1])
        return ['SET_SEG %s, %s' % (ops[0].lower(), REG16[ops[1]])]
    if mnem == 'MOV' and len(ops) == 2 and ops[1] in ('DS', 'ES'):
        if ops[0] in REG16:
            return ['GET_SEG %s, %s' % (REG16[ops[0]], ops[1].lower())]
        raise Untranslatable('MOV from ' + ops[1])
    if mnem == 'MOV' and len(ops) == 2 and ops[1] == 'SS':
        raise Untranslatable('MOV from SS')

    if mnem not in SIMPLE:
        raise Untranslatable('mnemonic ' + mnem)

    parsed = [Operand(o) for o in ops]
    setup = [line for p in parsed for line in p.setup]
    text = '%s %s' % (mnem.lower(), ', '.join(p.text for p in parsed))
    has_mem = any(p.is_mem for p in parsed)
    highs = [p.high for p in parsed if p.high]
    if has_mem and highs:
        if len(highs) > 1:
            raise Untranslatable('two high-byte registers with memory')
        high = highs[0]
        low = HIGH_TO_LOW[high]
        if re.search(r'\b%s\b' % low, text):
            raise Untranslatable('high and low byte of the same register with memory')
        text = re.sub(r'\b%s\b' % high, low, text)
        return setup + ['VIA_LOW %s, %s, {%s}' % (high, low, text)]
    return setup + [text]


def parse_listing(src):
    """The listing's instructions as (cs, raw bytes, mnemonic, operands) in
    listing order, and the procedure names by CS offset. Shared with asm2c.py."""
    rows = []
    proc_names = {}
    for line in open(src, encoding='ascii'):
        m = PROC_RE.match(line)
        if m:
            proc_names[int(m.group(2), 16)] = m.group(1)
            continue
        m = INSN_RE.match(line.rstrip('\n'))
        if m and not m.group(3) == 'db':
            rows.append((int(m.group(1), 16), m.group(2), m.group(3), m.group(4)))
    return rows, proc_names


def main():
    src, out = sys.argv[1], sys.argv[2]
    rows, proc_names = parse_listing(src)

    referenced = set()
    sp_slots = set()
    body = []
    traps = 0
    for cs, raw, mnem, args in rows:
        if cs in proc_names:
            body.append('')
            body.append('; ---- %s (CS:%04X)' % (proc_names[cs], cs))
        body.append('L_%04X:' % cs)
        for ln in MARKERS.get(cs, []):
            body.append('    ' + ln)
        if cs in CPU_PACE_NS:
            body.append('    CPU_PACE %d' % CPU_PACE_NS[cs])
        try:
            lines = translate(mnem, args, sp_slots)
        except Untranslatable as e:
            lines = ['TRAP 0x%04X   ; %s %s (%s)' % (cs, mnem, args.split('   ;')[0], e)]
            traps += 1
        for ln in lines:
            for lab in re.findall(r'L_([0-9A-F]{4})', ln):
                referenced.add(int(lab, 16))
            body.append('    ' + ln)

    defined = {cs for cs, *_ in rows}
    missing = sorted(x for x in referenced if x not in defined)
    if missing:
        sys.exit('asm2nasm: labels referenced but not in the listing: ' +
                 ' '.join('L_%04X' % x for x in missing))
    exported = sorted(set(proc_names) & defined)

    with open(out, 'w', encoding='ascii') as f:
        f.write('; GENERATED by tools/asm2nasm.py from re/SAB2.asm - do not edit.\n')
        f.write('; %d instructions, %d TRAPs.\n\n' % (len(defined), traps))
        f.write('%define RUNTIME_DEFINES_CS_LABELS\n%include "runtime.inc"\n\n')
        for x in exported:
            f.write('global L_%04X\n' % x)
        f.write('global cs_labels\n')
        f.write('\nsection .text\n')
        f.write('\n'.join(body))
        f.write('\n')
        # Offset of each instruction's label from cs_labels, 0 = no instruction.
        # In .text: a label difference within one section is a constant.
        f.write('\nalign 4\ncs_labels:\n')
        for cs in range(CODE_END + 1):
            f.write('    dd L_%04X - cs_labels\n' % cs if cs in defined else '    dd 0\n')
        if sp_slots:
            f.write('\nsection .bss\nalign 8\n')
            for slot in sorted(sp_slots):
                f.write('%s: resq 1\n' % slot)
    print('%s: %d instructions, %d procedures exported, %d TRAPs'
          % (out, len(defined), len(exported), traps))


if __name__ == '__main__':
    main()
