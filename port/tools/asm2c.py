#!/usr/bin/env python3
"""Translate the Ghidra listing of SAB2.COM (re/SAB2.asm) into C, for the web
(WebAssembly) build. The C counterpart of asm2nasm.py, whose listing parser
and tables it shares.

Usage: asm2c.py <re/SAB2.asm> <SAB2.COM> <version> <out.c>

The output is one function, game_run(): a switch on pc with one case per
instruction (its CS offset), in listing order, so straight-line code falls
through and every jump, call and return sets pc and continues the loop.
CALL pushes the CS offset of the next instruction on the runtime stack, RET
pops it into pc; JMP BX sets pc to BX. The register, flag and memory model
is described in src/runtime.h. After every BIOS, port or yielding C call the
function returns if game_yield is set, with pc where to continue, so the web
build can give control back to the browser and continue later.

The data segment image (SAB2.COM from file offset 2D60h, with the port's
changes of src/data.asm) is emitted as the ds_base array. Instructions the
translator does not handle become a call to game_trap(<cs>), which aborts at
runtime with the offset.
"""
import os
import re
import sys

# python3 -I leaves the script's directory off sys.path; no __pycache__ in tools/.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
from asm2nasm import (CPU_PACE_NS, IMM_RE, MARKERS, STRING_RE, Untranslatable,  # noqa: E402
                      parse_listing, split_operands, target_label)

CS_VALUE = 0x1000       # the code segment value (CS_VALUE in runtime.inc)

# The data segment as src/data.asm builds it: DS:0000..7A29 from SAB2.COM
# (file offset 2D60h) with the port's bytes patched in, the version text and
# menu item at VERSION_TEXT and the cheat keys screen at CHEATS_TEXT
# (runtime.inc). See data.asm for what each change is.
DS_FILE_OFFSET = 0x2D60
DS_IMAGE_SIZE = 0x7A2A
DATA_PATCHES = {
    0x0E21: [0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00],     # font: '.'
    0x5800: [0x00, 0x48, 0x08, 0x00, 0x00, 0x50, 0x04, 0x00,      # cursor keys
             0x00, 0x4B, 0x02, 0x00, 0x00, 0x4D, 0x01, 0x00],
}
VERSION_TEXT = 0x7A30
CHEATS_TEXT = 0x7A60


def version_text(version):
    return ([0xFE, 0x0A, 0xFD, 0x00, 0x00] + list(b'VER ' + version.encode('ascii')) +
            [0xFE, 0x08, 0xFD, 0x05, 0x10] + list(b'C  SEE CHEATS') + [0xFF])


CHEATS_LINES = [(0x04, b'CTRL I  INVINCIBLE'), (0x06, b'CTRL O  ONE HIT KILLS'),
                (0x08, b'CTRL P  ADD $100 PAY'), (0x0A, b'CTRL J  ADD 100 TIME'),
                (0x0C, b'CTRL K  HOLD KEYS')]


def cheats_text():
    out = [0xFE, 0x08, 0xFD, 0x04, 0x01] + list(b'CHEATS')
    for row, text in CHEATS_LINES:
        out += [0xFD, 0x00, row] + list(text)
    return out + [0xFF]


# Return points inside marker code get pc values right after the last
# instruction, which no instruction uses. This keeps the case range small:
# above 64K the switch's jump table outgrows a WebAssembly br_table (at most
# 65520 entries in V8) and the -O2 build dispatches wrongly.


def jump(target):
    return 'pc = 0x%04X; continue;' % target


def gate(flag, target):
    """CMP byte [flag], 0 / JNE target, as in the NASM markers."""
    return 'zf = !%s; cf = sf = of = af = 0; if (!zf) { pc = 0x%04X; continue; }' % (flag, target)


def skip_unless(flag):
    """CMP byte [flag], 0 / JE .skip, as in the NASM markers."""
    return 'if (!%s) { pc = {skip}; continue; }' % flag


# The same CS offsets and effects as asm2nasm.MARKERS (see the comments
# there). Items are C statements, ('call', cs) for a CALL from marker code,
# ('ccall', 'fn()') for a C call after which the game may yield,
# ('timer', wait) for a TIMER_POLL point and ('label', name) for a return
# point that statements reach as {name}. port_save / port_restore keep
# what the NASM markers keep around their calls, on the runtime stack.
MARKERS_C = {
    0x0620: [('timer', 1)],
    0x0270: [jump(0x0286)],
    0x039D: ['cheat_pay = 0;', 'cheat_time = 0;'],
    0x049A: [skip_unless('cheat_pay'), 'port_save();',
             'CX = (uint16_t)(take_u8(&cheat_pay) * 10);', ('call', 0x1F6B),
             'port_restore();', ('label', 'skip')],
    0x049D: [skip_unless('cheat_time'), 'port_save();', 'port_add_time();',
             'SI = 0x1BE1;', ('call', 0x04B7),
             'port_restore();', ('label', 'skip')],
    0x10CA: ['if (AL == 0x1B) AL = 0x03;'],
    0x112C: ['port_hold_keys();'],
    0x1F89: [gate('cheat_invincible', 0x1F90)],
    0x21D3: ['if (cheat_onehit) ds_base[0x6F98] = 1;'],
    0x2742: ['port_save();', 'SI = 0x%04X;' % VERSION_TEXT, ('call', 0x04B7), 'port_restore();'],
    0x275A: ['if (AL != 0x43) { pc = {skip}; continue; }', ('call', 0x2A93),
             'SI = 0x%04X;' % CHEATS_TEXT, ('call', 0x04B7), jump(0x28CC), ('label', 'skip')],
    0x2D0B: [('timer', 0)],
    0x2D54: [('timer', 1)],
}

REG16 = ('AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP')
REG8 = ('AL', 'BL', 'CL', 'DL', 'AH', 'BH', 'CH', 'DH')
SEG_BASE = {'DS': 'ds', 'ES': 'es'}

CONDITIONS = {
    'JZ': 'zf', 'JNZ': '!zf', 'JC': 'cf', 'JNC': '!cf', 'JA': '!cf && !zf', 'JBE': 'cf || zf',
    'JS': 'sf', 'JNS': '!sf', 'JL': 'sf != of', 'JGE': 'sf == of', 'JLE': 'zf || sf != of',
    'JG': '!zf && sf == of', 'JO': 'of', 'JNO': '!of',
}
ARITH = {'ADD': ('f_add', '0'), 'ADC': ('f_add', 'cf'), 'SUB': ('f_sub', '0'), 'SBB': ('f_sub', 'cf')}
LOGIC = {'AND': '&', 'OR': '|', 'XOR': '^'}
SHIFTS = {'SHL': 'SH_SHL', 'SHR': 'SH_SHR', 'SAR': 'SH_SAR', 'ROL': 'SH_ROL', 'ROR': 'SH_ROR',
          'RCL': 'SH_RCL', 'RCR': 'SH_RCR'}
# IRET pops what the timer delivery (runtime.c) pushes: return pc, flags.
NOARG = {'CLC': 'cf = 0;', 'STC': 'cf = 1;', 'CMC': 'cf ^= 1;', 'CLD': 'df = 0;', 'STD': 'df = 1;',
         'NOP': ';', 'CLI': ';', 'STI': ';', 'RET': 'pc = pop(); continue;',
         'IRET': 'pc = pop(); set_flags_image((uint16_t)pop()); continue;',
         'PUSHF': 'push(flags_image());', 'POPF': 'set_flags_image((uint16_t)pop());',
         'LAHF': 'f_lahf();', 'SAHF': 'f_sahf();', 'AAA': 'f_aaa();',
         'CBW': 'AX = (uint16_t)(int16_t)(int8_t)AL;'}


def target(text):
    return int(target_label(text)[2:], 16)


class Operand:
    """One operand as C: read() gives an expression, write(v) a statement.
    A memory operand with registers in its address needs `ea` declared by
    the caller (setup). Immediates are masked to the operand size."""

    def __init__(self, text):
        self.setup = None
        self.bits = None
        t = text.strip()
        m = re.match(r'(byte|word) ptr (.*)$', t)
        if m:
            self.bits = 8 if m.group(1) == 'byte' else 16
            t = m.group(2)
        seg, explicit = 'DS', False
        m = re.match(r'(ES|DS|CS|SS):(\[.*\])$', t)
        if m:
            seg, t, explicit = m.group(1), m.group(2), True
        if t.startswith('['):
            self.kind = 'mem'
            self._memory(t[1:-1], seg, explicit)
        elif t in REG16:
            self.kind, self.name, self.bits = 'reg', t, 16
        elif t in REG8:
            self.kind, self.name, self.bits = 'reg', t, 8
        elif re.fullmatch(IMM_RE, t):
            self.kind, self.value = 'imm', int(t, 16)
        elif t == 'CS':
            self.kind, self.value = 'imm', CS_VALUE
        else:
            raise Untranslatable('operand ' + text)

    def _memory(self, inner, seg, explicit):
        regs, disp = [], 0
        for term in (x.strip() for x in inner.split('+')):
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
        self.base = SEG_BASE[seg]
        if regs:
            self.setup = 'uint16_t ea = (uint16_t)(%s + 0x%x);' % (' + '.join(regs), disp & 0xFFFF)
            self.addr = 'ea'
        else:
            self.addr = '0x%x' % (disp & 0xFFFF)

    def read(self):
        if self.kind == 'mem':
            if self.bits == 8:
                return '%s[%s]' % (self.base, self.addr)
            return 'rd16(%s, %s)' % (self.base, self.addr)
        if self.kind == 'imm':
            return '0x%x' % (self.value & (0xFF if self.bits == 8 else 0xFFFF))
        return self.name

    def write(self, value):
        if self.kind == 'reg':
            return '%s = (uint%d_t)(%s);' % (self.name, self.bits, value)
        if self.kind == 'mem':
            if self.bits == 8:
                return '%s[%s] = (uint8_t)(%s);' % (self.base, self.addr, value)
            return 'wr16(%s, %s, %s);' % (self.base, self.addr, value)
        raise Untranslatable('write to an immediate')


def yield_check(resume):
    return 'if (game_yield) { pc = 0x%04X; return 1; }' % resume


def branch(cs, dest):
    """Jump to dest; backward jumps may end the slice in the web build."""
    if dest <= cs:
        return 'BACKEDGE(0x%04X); pc = 0x%04X; continue;' % (dest, dest)
    return jump(dest)


def translate(cs, nxt, mnem, args, sp_slots):
    """Return C lines for one listing instruction; nxt is the CS offset of the
    instruction after it."""
    args = args.split('   ;')[0].strip()

    if mnem == 'JMP' and args in REG16:
        return ['pc = %s; continue;' % args]
    if mnem == 'JMP':
        return [branch(cs, target(args))]
    if mnem in ('JP', 'JNP'):
        raise Untranslatable('parity flag')
    if mnem in CONDITIONS:
        return ['if (%s) { %s }' % (CONDITIONS[mnem], branch(cs, target(args)))]
    if mnem == 'JCXZ':
        return ['if (CX == 0) { %s }' % branch(cs, target(args))]
    if mnem == 'CALL':
        return ['push(0x%04X); pc = 0x%04X; continue;' % (nxt, target(args))]
    if mnem in ('LOOP', 'LOOPZ', 'LOOPNZ'):
        cond = {'LOOP': '', 'LOOPZ': ' && zf', 'LOOPNZ': ' && !zf'}[mnem]
        return ['if (--CX != 0%s) { %s }' % (cond, branch(cs, target(args)))]
    if mnem in NOARG and not args:
        return [NOARG[mnem]]
    if mnem == 'XLAT' and args == 'BX':
        return ['AL = ds[(uint16_t)(BX + AL)];']

    m = STRING_RE.match(mnem)
    if m:
        op, width, rep = m.groups()
        rep_code = {None: 0, 'REP': 1, 'REPE': 1, 'REPNE': 2}[rep]
        compares = 1 if op in ('SCAS', 'CMPS') else 0
        return ['STRING_OP(str_%s, %d, %d, %d)' % (op.lower(), 1 if width == 'B' else 2, rep_code, compares)]

    if mnem == 'INT':
        n = int(args, 16)
        if n in (0x10, 0x1A):
            return ['bios_int%02x();' % n, yield_check(nxt)]
        if n == 0x16:
            # Only ZF changes (BIOS_ZF in runtime.inc). A read with no key
            # waiting in the web build comes back to this INT.
            return ['{ uint32_t z = bios_int16(); if (z == BIOS_INT16_RETRY) { pc = 0x%04X; return 1; } '
                    'zf = (uint8_t)z; }' % cs, yield_check(nxt)]
        if n == 0x21:
            return ['bios_int21();', 'if (dos_exit) return 0;', yield_check(nxt)]
        raise Untranslatable('INT ' + args)
    if mnem == 'IN':
        dst, port = split_operands(args)
        if dst != 'AL' or port == 'DX':
            raise Untranslatable('IN ' + args)
        return ['cpu.port = %s;' % port, 'AL = (uint8_t)io_in();', yield_check(nxt)]
    if mnem == 'OUT':
        port, src = split_operands(args)
        if src != 'AL' or port == 'DX':
            raise Untranslatable('OUT ' + args)
        return ['cpu.port = %s;' % port, 'io_out();', yield_check(nxt)]

    if mnem in ('PUSH', 'POP'):
        if args in REG16:
            return ['push(%s);' % args] if mnem == 'PUSH' else ['%s = (uint16_t)pop();' % args]
        if args in ('DS', 'ES'):
            if mnem == 'PUSH':
                return ['push(seg_%s);' % args.lower()]
            return ['set_%s((uint16_t)pop());' % args.lower()]
        if mnem == 'PUSH' and args == 'CS':
            return ['push(0x%04X);' % CS_VALUE]
        raise Untranslatable(mnem + ' ' + args)

    ops = split_operands(args)
    # MOV [mem],SP / MOV SP,[mem] (sound_seq_play): the runtime stack index is
    # saved to and restored from a variable of its own, one per memory word.
    if 'SP' in ops:
        m = re.fullmatch(r'word ptr \[(0x[0-9a-f]+)\]', ops[1 if ops[0] == 'SP' else 0])
        if mnem != 'MOV' or len(ops) != 2 or not m:
            raise Untranslatable('SP operand')
        slot = 'sp_save_%04X' % int(m.group(1), 16)
        sp_slots.add(slot)
        return ['sp = %s;' % slot] if ops[0] == 'SP' else ['%s = sp;' % slot]
    if mnem == 'MOV' and len(ops) == 2 and ops[0] in ('DS', 'ES'):
        if ops[1] not in REG16:
            raise Untranslatable('MOV sreg from ' + ops[1])
        return ['set_%s(%s);' % (ops[0].lower(), ops[1])]
    if mnem == 'MOV' and len(ops) == 2 and ops[1] in ('ES', 'DS', 'SS'):
        if ops[0] in REG16 and ops[1] in ('DS', 'ES'):
            return ['%s = seg_%s;' % (ops[0], ops[1].lower())]
        raise Untranslatable('MOV from ' + ops[1])

    p = [Operand(o) for o in ops]
    setup = [x.setup for x in p if x.setup]
    bits = p[0].bits or (p[1].bits if len(p) > 1 else None)
    if bits is None:
        raise Untranslatable('operand size')
    for x in p:
        if x.bits is None:  # MOV [0x1bc3],AL or an immediate: size from the other operand
            x.bits = bits
    if len(p) > 1 and mnem in SHIFTS:
        p[1].bits = 8       # the count is CL or an immediate byte
    d = p[0]

    if mnem == 'MOV':
        body = [d.write(p[1].read())]
    elif mnem in ARITH:
        fn, carry = ARITH[mnem]
        body = [d.write('%s(%s, %s, %s, %d)' % (fn, d.read(), p[1].read(), carry, bits))]
    elif mnem == 'CMP':
        body = ['f_sub(%s, %s, 0, %d);' % (d.read(), p[1].read(), bits)]
    elif mnem in LOGIC:
        body = [d.write('f_logic(%s %s %s, %d)' % (d.read(), LOGIC[mnem], p[1].read(), bits))]
    elif mnem == 'TEST':
        body = ['f_logic(%s & %s, %d);' % (d.read(), p[1].read(), bits)]
    elif mnem in ('INC', 'DEC'):
        body = [d.write('f_%s(%s, %d)' % (mnem.lower(), d.read(), bits))]
    elif mnem == 'NOT':
        body = [d.write('~%s' % d.read())]
    elif mnem == 'NEG':
        body = [d.write('f_sub(0, %s, 0, %d)' % (d.read(), bits))]
    elif mnem in SHIFTS:
        body = [d.write('f_shift(%s, %s, %s, %d)' % (SHIFTS[mnem], d.read(), p[1].read(), bits))]
    elif mnem == 'MUL':
        body = ['f_mul%d(%s);' % (bits, d.read())]
    elif mnem == 'DIV':
        body = ['f_div%d(%s, 0x%04X);' % (bits, d.read(), cs)]
    elif mnem == 'XCHG':
        body = ['uint32_t t = %s;' % d.read(), d.write(p[1].read()), p[1].write('t')]
    else:
        raise Untranslatable('mnemonic ' + mnem)

    lines = setup + body
    if len(lines) == 1:
        return lines
    return ['{ ' + ' '.join(lines) + ' }']


def marker_lines(cs, items, synthetic, base):
    """C lines for one MARKERS_C entry at cs; synthetic is a list of the return
    points allocated so far (extended here), numbered from base. A
    ('case', n) item stands for a case label."""
    def new_point():
        n = base + len(synthetic)
        synthetic.append(n)
        return n

    labels = {name: new_point() for kind, name in (i for i in items if isinstance(i, tuple))
              if kind == 'label'}

    def fill(text):
        return re.sub(r'\{(\w+)\}', lambda m: '0x%X' % labels[m.group(1)], text)

    out = []
    for item in items:
        if isinstance(item, str):
            out.append(fill(item))
            continue
        kind, what = item
        if kind == 'label':
            out.append(('case', labels[what]))
        elif kind == 'call':
            resume = new_point()
            out.append('push(0x%X); pc = 0x%04X; continue;' % (resume, what))
            out.append(('case', resume))
        elif kind == 'ccall':
            resume = new_point()
            out.append('%s; %s' % (what, yield_check(resume)))
            out.append(('case', resume))
        elif kind == 'timer':
            # Deliver each due tick as an interrupt (runtime.c); after a
            # sleep in the web build, poll again from the start.
            deliver, done = new_point(), new_point()
            out.append('if (timer_poll_begin(%d)) { pc = 0x%X; continue; }' % (what, deliver))
            out.append(yield_check(cs))
            out.append('pc = 0x%X; continue;' % done)
            out.append(('case', deliver))
            out.append('if (timer_poll_next()) { push(flags_image()); push(0x%X); '
                       'pc = timer_vector; continue; }' % deliver)
            out.append(('case', done))
        else:
            raise ValueError(kind)
    return out


def main():
    src, com, version, out = sys.argv[1:5]
    if set(MARKERS_C) != set(MARKERS):
        sys.exit('asm2c.py: MARKERS_C and asm2nasm.MARKERS differ at %s' % ', '.join(
            '%04X' % x for x in sorted(set(MARKERS_C) ^ set(MARKERS))))
    rows, proc_names = parse_listing(src)

    defined = {cs for cs, *_ in rows}
    body = []
    synthetic = []
    synthetic_base = max(cs + len(raw) // 2 for cs, raw, *_ in rows)
    sp_slots = set()
    traps = 0
    for cs, raw, mnem, args in rows:
        if cs in proc_names:
            body.append('')
            body.append('    /* ---- %s (CS:%04X) */' % (proc_names[cs], cs))
        body.append('    case 0x%04X:' % cs)
        items = list(MARKERS_C.get(cs, []))
        if cs in CPU_PACE_NS:
            items.append(('ccall', 'cpu_pace(%d)' % CPU_PACE_NS[cs]))
        for ln in marker_lines(cs, items, synthetic, synthetic_base):
            if isinstance(ln, tuple):
                body.append('    case 0x%X:' % ln[1])
            else:
                body.append('        ' + ln)
        nxt = cs + len(raw) // 2
        try:
            lines = translate(cs, nxt, mnem, args, sp_slots)
        except Untranslatable as e:
            lines = ['game_trap(0x%04X);   /* %s %s (%s) */' % (cs, mnem, args.split('   ;')[0], e)]
            traps += 1
        for ln in lines:
            for ref in re.findall(r'pc = 0x([0-9A-F]{4});', ln):
                if int(ref, 16) not in defined:
                    sys.exit('asm2c.py: CS:%04X transfers to CS:%s, which is not an instruction' % (cs, ref))
            body.append('        ' + ln)
    last_cs = rows[-1][0]
    body.append('        game_trap(0x%04X);   /* ran past the last instruction */' % last_cs)

    image = bytearray(open(com, 'rb').read()[DS_FILE_OFFSET:DS_FILE_OFFSET + DS_IMAGE_SIZE])
    if len(image) != DS_IMAGE_SIZE:
        sys.exit('asm2c.py: %s is too short for the data segment image' % com)
    for offset, data in DATA_PATCHES.items():
        image[offset:offset + len(data)] = bytes(data)
    image += bytes(VERSION_TEXT - len(image)) + bytes(version_text(version))
    if len(image) > CHEATS_TEXT:
        sys.exit('asm2c.py: the version text runs into CHEATS_TEXT')
    image += bytes(CHEATS_TEXT - len(image)) + bytes(cheats_text())

    with open(out, 'w', encoding='ascii') as f:
        f.write('/* GENERATED by tools/asm2c.py from re/SAB2.asm and SAB2.COM - do not edit.\n')
        f.write(' * %d instructions, %d traps. */\n\n' % (len(defined), traps))
        f.write('#include "runtime.h"\n\n')
        f.write('/* The data segment (DS = CS + 2E6h) as src/data.asm builds it: DS:0000..%04X,\n'
                ' * the rest zero. */\n' % (len(image) - 1))
        f.write('uint8_t ds_base[SEGMENT_SIZE] = {\n')
        for i in range(0, len(image), 16):
            f.write('    %s,\n' % ', '.join('0x%02x' % b for b in image[i:i + 16]))
        f.write('};\n\n')
        for slot in sorted(sp_slots):
            f.write('static unsigned %s;\n' % slot)
        f.write('\n#ifdef __EMSCRIPTEN__\n'
                '/* Every 65536th backward jump checks whether the slice is used up, so a\n'
                ' * loop without BIOS or port calls cannot keep the browser waiting. */\n'
                'static uint16_t backedges;\n'
                '#define BACKEDGE(t) if (!++backedges && bios_slice_over()) { pc = (t); return 1; }\n'
                '#else\n'
                '#define BACKEDGE(t)\n'
                '#endif\n\n')
        f.write('int game_run(void)\n{\n    for (;;) {\n    switch (pc) {\n')
        f.write('\n'.join(body))
        f.write('\n    default:\n'
                '        if (pc == UINT32_MAX)   /* the entry code returned (see game_start) */\n'
                '            return 0;\n'
                '        game_bad_jump(pc);\n'
                '    }\n    }\n}\n')
    print('%s: %d instructions, %d marker return points, %d traps'
          % (out, len(defined), len(synthetic), traps))


if __name__ == '__main__':
    main()
