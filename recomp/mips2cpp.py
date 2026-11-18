#!/usr/bin/env python3
"""Static recompiler: R5900 (PS2 EE) code of the MGO1 ntmenu2 overlay -> C++.

Each function becomes `void F_<addr>(Ctx& c)`; memory goes through the
runtime (ps2rt.h), calls through `Call(c, addr)` (recompiled function or
bridge), indirect jumps through a per-function switch on known labels.

    python mips2cpp.py <overlay.bin> <base hex> <from hex> <to hex> <out.cpp>
"""
import os
import struct, sys

MASK32 = 0xffffffff


def s16(x): return x - 0x10000 if x & 0x8000 else x


class Fn:
    def __init__(self, start, end):
        self.start, self.end = start, end


def find_functions(words, base, lo, hi):
    starts = set()
    for i in range((lo - base) // 4, (hi - base) // 4):
        w = words[i]
        if (w & 0xffff0000) == 0x27bd0000 and (w & 0x8000):       # addiu sp, sp, -n
            starts.add(base + i * 4)
    for w in words:
        if (w >> 26) == 3:
            t = (w & 0x3ffffff) << 2
            if lo <= t < hi:
                starts.add(t)
    # entries reached only through pointers: lui/addiu pairs and data words
    # that land right after a `jr ra` + delay slot of the previous function
    JR_RA = 0x03e00008
    def after_return(t):
        k = (t - base) // 4
        return k >= 2 and words[k - 2] == JR_RA
    last = {}
    for i, w in enumerate(words):
        op = w >> 26
        if op == 0x0f:
            last[(w >> 16) & 31] = (i, (w & 0xffff) << 16)
        elif op in (0x09, 0x0d) and ((w >> 21) & 31) in last and i - last[(w >> 21) & 31][0] < 16:
            imm = w & 0xffff
            if op == 0x09 and imm & 0x8000: imm -= 0x10000
            t = (last[(w >> 21) & 31][1] + imm) & 0xffffffff
            if lo <= t < hi and t % 4 == 0 and after_return(t):
                starts.add(t)
        if lo <= w < hi and w % 4 == 0 and after_return(w):
            starts.add(w)
    starts = sorted(starts)
    return [Fn(s, starts[k + 1] if k + 1 < len(starts) else hi) for k, s in enumerate(starts)]


R = lambda n: f'c.r[{n}]'
S32 = lambda e: f'(int64_t)(int32_t)({e})'


class Lit:
    """An instruction's fields as literals: the function recompiled for its own address."""
    def __init__(self, a, w):
        self.a, self.w = a, w
    def simm(self): return f'{s16(self.w & 0xffff)}'
    def imm(self): return f'{self.w & 0xffff:#x}ull'
    def lui(self): return f'{((self.w & 0xffff) << 16) & MASK32:#x}u'
    def pc(self, k=0): return f'{self.a + k:#x}'


class Par(Lit):
    """A shape's instruction (recomp/mips2shape.py): its address is the instance's A + off, its
    relocated fields are read from the instance's own code (IMM, UIMM, HI: ps2rt.h)."""
    def __init__(self, a, w, off, masked):
        Lit.__init__(self, a, w)
        self.off, self.masked = off, masked
    def simm(self): return f'IMM({self.off:#x})' if self.masked else Lit.simm(self)
    def imm(self): return f'(uint64_t)UIMM({self.off:#x})' if self.masked else Lit.imm(self)
    def lui(self): return f'HI({self.off:#x})'
    def pc(self, k=0): return f'(A + {self.off + k:#x})'


def gen_ins(a, w, fn, labels, FP=None):
    """Returns (kind, code): kind is None for straight code, or a control
    record ('br', cond, target, likely) / ('j', target) / ('jal', target) /
    ('jr', reg) / ('jalr', rd, rs). FP: the fields (Lit: literals; Par: a shape's)."""
    op, rs, rt, rd, sa, fu = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    imm, simm = w & 0xffff, s16(w & 0xffff)
    if FP is None: FP = Lit(a, w)
    tgt = lambda: a + 4 + (simm << 2)
    def setr(n, e):
        return '' if n == 0 else f'{R(n)} = {e};'
    if w == 0:
        return None, ''
    if op == 0:                                     # SPECIAL
        if fu == 0x00: return None, setr(rd, S32(f'(uint32_t){R(rt)} << {sa}'))
        if fu == 0x02: return None, setr(rd, S32(f'(uint32_t){R(rt)} >> {sa}'))
        if fu == 0x03: return None, setr(rd, S32(f'(int32_t){R(rt)} >> {sa}'))
        if fu == 0x04: return None, setr(rd, S32(f'(uint32_t){R(rt)} << ({R(rs)} & 31)'))
        if fu == 0x06: return None, setr(rd, S32(f'(uint32_t){R(rt)} >> ({R(rs)} & 31)'))
        if fu == 0x07: return None, setr(rd, S32(f'(int32_t){R(rt)} >> ({R(rs)} & 31)'))
        if fu == 0x08: return ('jr', rs), ''
        if fu == 0x09: return ('jalr', rd, rs), ''
        if fu == 0x0a: return None, f'if ({R(rt)} == 0) {{ {setr(rd, R(rs))} }}'
        if fu == 0x0b: return None, f'if ({R(rt)} != 0) {{ {setr(rd, R(rs))} }}'
        if fu == 0x0c: return None, f'Syscall(c, {FP.pc()});'             # the number in $v1: the platform serves it
        if fu == 0x0d: return None, f'Trap(c, {FP.pc()});'
        if fu == 0x0f: return None, ''                                  # sync
        if fu == 0x10: return None, setr(rd, 'c.hi')
        if fu == 0x11: return None, f'c.hi = {R(rs)};'
        if fu == 0x12: return None, setr(rd, 'c.lo')
        if fu == 0x13: return None, f'c.lo = {R(rs)};'
        if fu == 0x14: return None, setr(rd, f'{R(rt)} << ({R(rs)} & 63)')
        if fu == 0x16: return None, setr(rd, f'{R(rt)} >> ({R(rs)} & 63)')
        if fu == 0x17: return None, setr(rd, f'(uint64_t)((int64_t){R(rt)} >> ({R(rs)} & 63))')
        if fu == 0x18:
            return None, (f'{{ int64_t p = (int64_t)(int32_t){R(rs)} * (int64_t)(int32_t){R(rt)}; '
                          f'c.lo = (int64_t)(int32_t)p; c.hi = (int64_t)(int32_t)(p >> 32); {setr(rd, "c.lo")} }}')
        if fu == 0x19:
            return None, (f'{{ uint64_t p = (uint64_t)(uint32_t){R(rs)} * (uint64_t)(uint32_t){R(rt)}; '
                          f'c.lo = (int64_t)(int32_t)p; c.hi = (int64_t)(int32_t)(p >> 32); {setr(rd, "c.lo")} }}')
        if fu == 0x1a:                              # div: the EE's results for INT_MIN / -1 and for / 0 (PCSX2)
            return None, (f'{{ int32_t n = (int32_t){R(rs)}, d = (int32_t){R(rt)}; '
                          f'if (n == INT32_MIN && d == -1) {{ c.lo = (int64_t)INT32_MIN; c.hi = 0; }} '
                          f'else if (d != 0) {{ c.lo = (int64_t)(n / d); c.hi = (int64_t)(n % d); }} '
                          f'else {{ c.lo = (int64_t)(n < 0 ? 1 : -1); c.hi = (int64_t)n; }} }}')
        if fu == 0x1b:
            return None, (f'{{ uint32_t n = (uint32_t){R(rs)}, d = (uint32_t){R(rt)}; if (d != 0) '
                          f'{{ c.lo = (int64_t)(int32_t)(n / d); c.hi = (int64_t)(int32_t)(n % d); }} '
                          f'else {{ c.lo = (uint64_t)(int64_t)-1; c.hi = (int64_t)(int32_t)n; }} }}')
        if fu == 0x20 or fu == 0x21: return None, setr(rd, S32(f'(uint32_t){R(rs)} + (uint32_t){R(rt)}'))
        if fu == 0x22 or fu == 0x23: return None, setr(rd, S32(f'(uint32_t){R(rs)} - (uint32_t){R(rt)}'))
        if fu == 0x24: return None, setr(rd, f'{R(rs)} & {R(rt)}')
        if fu == 0x25: return None, setr(rd, f'{R(rs)} | {R(rt)}')
        if fu == 0x26: return None, setr(rd, f'{R(rs)} ^ {R(rt)}')
        if fu == 0x27: return None, setr(rd, f'~({R(rs)} | {R(rt)})')
        if fu == 0x28: return None, f'{R(rd)} = c.sa; ' if rd else ''       # mfsa
        if fu == 0x29: return None, f'c.sa = (uint32_t){R(rs)};'               # mtsa
        if fu == 0x2a: return None, setr(rd, f'(int64_t){R(rs)} < (int64_t){R(rt)} ? 1 : 0')
        if fu == 0x2b: return None, setr(rd, f'{R(rs)} < {R(rt)} ? 1 : 0')
        if fu == 0x2c or fu == 0x2d: return None, setr(rd, f'{R(rs)} + {R(rt)}')
        if fu == 0x2e or fu == 0x2f: return None, setr(rd, f'{R(rs)} - {R(rt)}')
        if fu in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36): return None, f'if (TrapCond(c, {fu}, {R(rs)}, {R(rt)})) Trap(c, {FP.pc()});'
        if fu == 0x38: return None, setr(rd, f'{R(rt)} << {sa}')
        if fu == 0x3a: return None, setr(rd, f'{R(rt)} >> {sa}')
        if fu == 0x3b: return None, setr(rd, f'(uint64_t)((int64_t){R(rt)} >> {sa})')
        if fu == 0x3c: return None, setr(rd, f'{R(rt)} << {sa + 32}')
        if fu == 0x3e: return None, setr(rd, f'{R(rt)} >> {sa + 32}')
        if fu == 0x3f: return None, setr(rd, f'(uint64_t)((int64_t){R(rt)} >> {sa + 32})')
        return None, f'Unimpl(c, {FP.pc()}, {w:#010x});'
    if op == 1:                                     # REGIMM
        conds = {0: f'(int64_t){R(rs)} < 0', 1: f'(int64_t){R(rs)} >= 0', 2: f'(int64_t){R(rs)} < 0', 3: f'(int64_t){R(rs)} >= 0',
                 0x10: f'(int64_t){R(rs)} < 0', 0x11: f'(int64_t){R(rs)} >= 0',
                 0x12: f'(int64_t){R(rs)} < 0', 0x13: f'(int64_t){R(rs)} >= 0'}
        if rt in conds:
            link = rt in (0x10, 0x11, 0x12, 0x13)
            return ('br', conds[rt], tgt(), rt in (2, 3, 0x12, 0x13), link), ''
        if rt == 0x18: return None, f'c.sa = ((uint32_t){R(rs)} & 0xf) ^ {w & 0xf};'           # mtsab
        if rt == 0x19: return None, f'c.sa = (((uint32_t){R(rs)} & 7) ^ {w & 7}) << 1;'       # mtsah
        return None, f'Unimpl(c, {FP.pc()}, {w:#010x});'
    if op == 2: return ('j', (a & 0xf0000000) | ((w & 0x3ffffff) << 2)), ''
    if op == 3: return ('jal', (a & 0xf0000000) | ((w & 0x3ffffff) << 2)), ''
    if op in (4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
        c = {4: f'{R(rs)} == {R(rt)}', 5: f'{R(rs)} != {R(rt)}', 6: f'(int64_t){R(rs)} <= 0', 7: f'(int64_t){R(rs)} > 0'}[op & 0x7 | 4 if op >= 0x14 else op]
        return ('br', c, tgt(), op >= 0x14, False), ''
    if op == 8 or op == 9: return None, setr(rt, S32(f'(uint32_t){R(rs)} + (uint32_t){FP.simm()}'))
    if op == 0x0a: return None, setr(rt, f'(int64_t){R(rs)} < (int64_t){FP.simm()} ? 1 : 0')
    if op == 0x0b: return None, setr(rt, f'{R(rs)} < (uint64_t)(int64_t){FP.simm()} ? 1 : 0')
    if op == 0x0c: return None, setr(rt, f'{R(rs)} & {FP.imm()}')
    if op == 0x0d: return None, setr(rt, f'{R(rs)} | {FP.imm()}')
    if op == 0x0e: return None, setr(rt, f'{R(rs)} ^ {FP.imm()}')
    if op == 0x0f: return None, setr(rt, f'(uint64_t)(int64_t)(int32_t){FP.lui()}')
    if op == 0x10:                                  # COP0: interrupt enable and status
        if rs == 0x00:                              # mfc0: Status carries EIE (bit 16) as di / ei left it
            return None, setr(rt, 'Cop0Status()' if rd == 12 else '0')
        if rs == 0x04: return None, ';'            # mtc0
        if rs == 0x10 and fu == 0x38: return None, 'Cop0Ei();'
        if rs == 0x10 and fu == 0x39: return None, 'Cop0Di();'
        if rs == 0x10 and fu in (0x02, 0x06, 0x08): return None, ';'   # tlbwi, tlbwr, tlbp
        if rs == 0x08: return None, ';'             # bc0f / bc0t style waits: never taken
        return None, f'Unimpl(c, {FP.pc()}, {w:#010x});'
    if op == 0x11:                                  # COP1
        fmt, ft, fs, fd = rs, rt, rd, sa
        F = lambda n: f'c.f[{n}]'
        if fmt == 0x00: return None, setr(ft, f'(int64_t)(int32_t)FBits({F(fs)})')      # mfc1
        if fmt == 0x04: return None, f'{F(fs)} = BitsF((uint32_t){R(rt)});'             # mtc1
        if fmt == 0x02: return None, setr(ft, '0')                                       # cfc1
        if fmt == 0x06: return None, ''                                                  # ctc1
        if fmt == 0x08:
            tf, lk = rt & 1, (rt >> 1) & 1
            return ('br', 'c.fcc' if tf else '!c.fcc', tgt(), bool(lk), False), ''
        if fmt == 0x10:
            # (the PS2's FPU saturates at the largest float: FC, ps2rt.h)
            m = {0x00: f'{F(fd)} = FC({F(fs)} + {F(ft)});', 0x01: f'{F(fd)} = FC({F(fs)} - {F(ft)});',
                 0x02: f'{F(fd)} = FC({F(fs)} * {F(ft)});', 0x03: f'{F(fd)} = FDiv({F(fs)}, {F(ft)});',
                 0x04: f'{F(fd)} = FSqrt({F(ft)});', 0x05: f'{F(fd)} = fabsf({F(fs)});',
                 0x06: f'{F(fd)} = {F(fs)};', 0x07: f'{F(fd)} = -{F(fs)};',
                 0x16: f'{F(fd)} = FRsqrt({F(fs)}, {F(ft)});',
                 0x18: f'c.acc = FC({F(fs)} + {F(ft)});', 0x19: f'c.acc = FC({F(fs)} - {F(ft)});', 0x1a: f'c.acc = FC({F(fs)} * {F(ft)});',
                 0x1c: f'{F(fd)} = FC(c.acc + FC({F(fs)} * {F(ft)}));', 0x1d: f'{F(fd)} = FC(c.acc - FC({F(fs)} * {F(ft)}));',
                 0x1e: f'c.acc = FC(c.acc + FC({F(fs)} * {F(ft)}));', 0x1f: f'c.acc = FC(c.acc - FC({F(fs)} * {F(ft)}));',
                 0x24: f'{F(fd)} = BitsF((uint32_t)FToW({F(fs)}));',
                 0x28: f'{F(fd)} = fmaxf({F(fs)}, {F(ft)});', 0x29: f'{F(fd)} = fminf({F(fs)}, {F(ft)});',
                 0x30: 'c.fcc = false;', 0x32: f'c.fcc = {F(fs)} == {F(ft)};', 0x34: f'c.fcc = {F(fs)} < {F(ft)};',
                 0x36: f'c.fcc = {F(fs)} <= {F(ft)};'}
            if fu in m: return None, m[fu]
        if fmt == 0x14 and fu == 0x20: return None, f'{F(fd)} = (float)(int32_t)FBits({F(fs)});'   # cvt.s.w
        return None, f'Unimpl(c, {FP.pc()}, {w:#010x});'
    if op == 0x12:                                  # COP2: VU0 in macro mode (vu0.cpp)
        if rs == 0x01: return None, f'Qmfc2(c, {rt}, {rd});'
        if rs == 0x05: return None, f'Qmtc2(c, {rt}, {rd});'
        if rs == 0x02: return None, setr(rt, f'(int64_t)(int32_t)Vu0Cfc2({rd})')
        if rs == 0x06: return None, f'Vu0Ctc2({rd}, (uint32_t){R(rt)});'
        if rs == 0x08:                              # bc2f / bc2t: VU0 is never busy (microprograms run to their end)
            tf, lk = rt & 1, (rt >> 1) & 1
            return ('br', 'false' if tf else 'true', tgt(), bool(lk), False), ''
        if rs >= 0x10: return None, f'Vu0Macro(c, {w:#010x}u, {FP.pc()});'
        return None, f'Unimpl(c, {FP.pc()}, {w:#010x});'
    if op == 0x19: return None, setr(rt, f'{R(rs)} + (uint64_t)(int64_t){FP.simm()}')                   # daddiu
    ea = f'(uint32_t)({R(rs)} + (int64_t){FP.simm()})'
    loads = {0x20: ('R8', '(int64_t)(int8_t)'), 0x24: ('R8', '(uint64_t)'), 0x21: ('R16', '(int64_t)(int16_t)'),
             0x25: ('R16', '(uint64_t)'), 0x23: ('R32', '(int64_t)(int32_t)'), 0x27: ('R32', '(uint64_t)'),
             0x37: ('R64', '(uint64_t)')}
    if op in loads:
        fnm, cast = loads[op]
        return None, setr(rt, f'{cast}{fnm}({ea})') if rt else f'(void){fnm}({ea});'
    stores = {0x28: 'W8', 0x29: 'W16', 0x2b: 'W32', 0x3f: 'W64'}
    if op in stores: return None, f'{stores[op]}({ea}, {R(rt)});'
    if op == 0x1a: return None, setr(rt, f'LDL({ea}, {R(rt)})')
    if op == 0x1b: return None, setr(rt, f'LDR({ea}, {R(rt)})')
    if op == 0x22: return None, setr(rt, f'LWL({ea}, {R(rt)})')
    if op == 0x26: return None, setr(rt, f'LWR({ea}, {R(rt)})')
    if op == 0x2c: return None, f'SDL({ea}, {R(rt)});'
    if op == 0x2d: return None, f'SDR({ea}, {R(rt)});'
    if op == 0x2a: return None, f'SWL({ea}, {R(rt)});'
    if op == 0x2e: return None, f'SWR({ea}, {R(rt)});'
    if op == 0x31: return None, f'c.f[{rt}] = BitsF(R32({ea}));'
    if op == 0x39: return None, f'W32({ea}, FBits(c.f[{rt}]));'
    if op == 0x36: return None, f'Lqc2({rt}, {ea});'                                                # lqc2
    if op == 0x3e: return None, f'Sqc2({rt}, {ea});'                                                # sqc2
    if op == 0x1e: return None, f'LQ(c, {rt}, {ea});'
    if op == 0x1f: return None, f'SQ(c, {rt}, {ea});'
    if op == 0x2f: return None, ''                                                                  # cache
    if op == 0x33: return None, ''                                                                  # pref
    if op == 0x1c: return None, f'MMI(c, {FP.pc()}, {w:#010x});'
    return None, f'Unimpl(c, {FP.pc()}, {w:#010x});'


PREFIX = ''                 # page name of an overlay built on its own (functions F_<page>_<addr>)
# instruction variants the platform switches at run time (recomp/variants.txt): for the page being
# generated, address -> [(kind, value, id)]; 'word' runs another instruction word instead,
# 'after' / 'before' run a C++ statement after / before the instruction, while ps2::gVariant[id] is set
VARIANTS = {}


def load_variants(page):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'variants.txt')
    if not os.path.exists(path):
        return {}
    out = {}
    for line in open(path):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        pg, addr, kind, rest = line.split(None, 3)
        if pg != page:
            continue
        value, vid = rest.rsplit(None, 1)
        out.setdefault(int(addr, 16), []).append((kind, int(value, 16) if kind == 'word' else value, int(vid)))
    return out


def fname(a):
    return f'F_{PREFIX}_{a:08x}' if PREFIX and a >= 0x2DA680 else f'F_{a:08x}'


def emit_fn(out, fn, words, base, fnset):
    n = (fn.end - fn.start) // 4
    ins = [(fn.start + k * 4, words[(fn.start - base) // 4 + k]) for k in range(n)]
    code = {}
    targets = set()
    uses_variant = False
    for a, w in ins:
        kind, c = gen_ins(a, w, fn, None)
        for vk, vv, vid in VARIANTS.get(a, []):
            if kind is not None:
                raise SystemExit(f'variant at {a:#x}: not a straight instruction')
            if vk == 'word':
                k2, c2 = gen_ins(a, vv, fn, None)
                if k2 is not None:
                    raise SystemExit(f'variant at {a:#x}: the other word is not a straight instruction')
                c = f'if (gVariant[{vid}]) {{ {c2} }} else {{ {c} }}'
            elif vk == 'after':
                c = f'{c} if (gVariant[{vid}]) {{ {vv} }}'
            elif vk == 'before':
                c = f'if (gVariant[{vid}]) {{ {vv} }} {c}'
            else:
                raise SystemExit(f'variant at {a:#x}: kind {vk}')
            uses_variant = True
        code[a] = (kind, c)
        if kind and kind[0] == 'br' and fn.start <= kind[2] < fn.end: targets.add(kind[2])
        if kind and kind[0] == 'j' and fn.start <= kind[1] < fn.end: targets.add(kind[1])
    if uses_variant:
        out.append('extern unsigned char gVariant[64];')
    out.append(f'void {fname(fn.start)}(Ctx& c) {{')
    has_jr = any(k and k[0] == 'jr' and k[1] != 31 for k, _ in code.values())
    skip = set()
    for idx, (a, w) in enumerate(ins):
        if a in skip:
            continue
        kind, c = code[a]
        lab = f'L_{a:08x}: ' if (a in targets or has_jr) else ''
        if kind is None:
            out.append(f'  {lab}{c}')
            continue
        dly = code.get(a + 4, (None, ''))
        dcode = dly[1] if dly[0] is None else f'/* control in delay slot at {a + 4:#x} */'
        skip.add(a + 4)
        if kind[0] == 'br':
            _, cond, t, likely, link = kind
            jump = ('{ ' + ('BACKEDGE(); ' if t <= a else '') + f'goto L_{t:08x}; }}') if fn.start <= t < fn.end else f'{{ Call(c, {t:#x}); return; }}'
            linkc = f'{R(31)} = {a + 8:#x};' if link else ''
            if likely:
                out.append(f'  {lab}if ({cond}) {{ {linkc} {dcode} {jump} }}')
            else:
                out.append(f'  {lab}{{ bool t_ = {cond}; {linkc} {dcode} if (t_) {jump} }}')
            if a + 4 in targets or has_jr:
                out.append(f'  L_{a + 4:08x}: ;')
        elif kind[0] == 'j':
            t = kind[1]
            jump = ('{ ' + ('BACKEDGE(); ' if t <= a else '') + f'goto L_{t:08x}; }}') if fn.start <= t < fn.end else f'{{ Call(c, {t:#x}); return; }}'
            out.append(f'  {lab}{{ {dcode} {jump} }}')
            if a + 4 in targets or has_jr: out.append(f'  L_{a + 4:08x}: ;')
        elif kind[0] == 'jal':
            out.append(f'  {lab}{{ {dcode} {R(31)} = {a + 8:#x}; Call(c, {kind[1]:#x}); }}')
            if a + 4 in targets or has_jr: out.append(f'  L_{a + 4:08x}: ;')
        elif kind[0] == 'jr':
            rs = kind[1]
            if rs == 31:
                out.append(f'  {lab}{{ {dcode} return; }}')
            else:
                cases = ' '.join(f'case {x:#x}: goto L_{x:08x};' for x, _ in ins)
                out.append(f'  {lab}{{ uint32_t t_ = (uint32_t){R(rs)}; {dcode} switch (t_) {{ {cases} default: Call(c, t_); return; }} }}')
            if a + 4 in targets or has_jr: out.append(f'  L_{a + 4:08x}: ;')
        elif kind[0] == 'jalr':
            _, rd, rs = kind
            out.append(f'  {lab}{{ uint32_t t_ = (uint32_t){R(rs)}; {dcode} {R(rd)} = {a + 8:#x}; Call(c, t_); }}')
            if a + 4 in targets or has_jr: out.append(f'  L_{a + 4:08x}: ;')
    # a function that does not end on a jump falls into the next one
    last = ins[-1][0] if ins else fn.start
    lk = code.get(last - 4, (None, ''))[0] if last - 4 in code else None
    if not (lk and lk[0] in ('j', 'jr')) and not (code.get(last, (None, ''))[0] and code[last][0][0] in ('j', 'jr')):
        out.append(f'  Call(c, {fn.end:#x}); return;')
    out.append('}')


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--overlay', required=True)
    ap.add_argument('--base', required=True)
    ap.add_argument('--functions', required=True, help='closure.py output: start end per line')
    ap.add_argument('--out', required=True, help='output prefix')
    ap.add_argument('--chunk', type=int, default=250)
    # several page overlays in one build: the base functions once (--select base), each
    # page's own functions under its name (--select overlay --prefix <page>), each with
    # its table; --select all keeps the single table of the ntmenu2-only builds
    ap.add_argument('--select', choices=['all', 'base', 'overlay'], default='all')
    ap.add_argument('--prefix', default='')
    ap.add_argument('--alias', help='tools/page_alias.py output: the page\'s copies of functions served at ntmenu2 addresses')
    a = ap.parse_args()
    global PREFIX, VARIANTS
    PREFIX = a.prefix if a.select == 'overlay' else ''
    VARIANTS = load_variants(PREFIX or ('base' if a.select == 'base' else 'ntmenu2'))
    imgs = []
    for path, b in ((a.overlay, 0x2DA680), (a.base, 0x100000)):
        blob = open(path, 'rb').read()
        imgs.append((b, struct.unpack_from(f'<{len(blob) // 4}I', blob)))

    class Words:
        def __getitem__(self, k):
            addr = k                      # absolute address / 4 index trick below
            raise IndexError
    fns = []
    for line in open(a.functions):
        if line.strip():
            st, en = line.split()[:2]
            fns.append(Fn(int(st, 16), int(en, 16)))
    fns.sort(key=lambda f: f.start)
    if a.select == 'base': fns = [f for f in fns if f.start < 0x2DA680]
    if a.select == 'overlay': fns = [f for f in fns if f.start >= 0x2DA680]

    def fn_words(f):
        for b, w in imgs:
            k = (f.start - b) // 4
            if 0 <= k < len(w):
                n = (f.end - f.start) // 4
                return b, w
        return None, None
    import os
    hdr = [f'// generated by mips2cpp.py, do not edit', '#pragma once', '#include "ps2rt.h"', 'namespace ps2 {']
    hdr += [f'void {fname(f.start)}(Ctx& c);' for f in fns]
    hdr.append('}  // namespace ps2')
    open(a.out + '_decls.h', 'w').write('\n'.join(hdr) + '\n')
    files = 0
    for k in range(0, len(fns), a.chunk):
        out = ['// generated by mips2cpp.py, do not edit', f'#include "{os.path.basename(a.out)}_decls.h"', 'namespace ps2 {']
        for f in fns[k:k + a.chunk]:
            b, w = fn_words(f)
            if w is None:
                out.append(f'void {fname(f.start)}(Ctx& c) {{ Unimpl(c, {f.start:#x}, 0); }}')
                continue
            emit_fn(out, f, w, b, None)
        out.append('}  // namespace ps2')
        open(f'{a.out}_{files:02d}.cpp', 'w').write('\n'.join(out) + '\n')
        files += 1
    tab = ['// generated by mips2cpp.py, do not edit', f'#include "{os.path.basename(a.out)}_decls.h"', 'namespace ps2 {']
    if a.select == 'overlay':
        tab.append('static const FnEntry kFns[] = {')
        tab += [f'  {{{f.start:#x}, {fname(f.start)}}},' for f in fns]
        alias = []
        if a.alias and os.path.exists(a.alias):
            alias = sorted(tuple(int(x, 16) for x in l.split()[:2]) for l in open(a.alias) if l.strip() and not l.startswith('#'))
        if alias:
            tab.append('};')
            tab.append('static const AliasEntry kAlias[] = {')
            tab += [f'  {{{p:#x}, {q:#x}}},' for p, q in alias]
            tab += ['};', f'static OverlayRegistration kReg("{a.prefix}", kFns, {len(fns)}, kAlias, {len(alias)});', '}  // namespace ps2']
        else:
            tab += ['};', f'static OverlayRegistration kReg("{a.prefix}", kFns, {len(fns)});', '}  // namespace ps2']
    else:
        tab.append('const FnEntry kFunctions[] = {')
        tab += [f'  {{{f.start:#x}, {fname(f.start)}}},' for f in fns]
        tab += ['};', f'const int kFunctionCount = {len(fns)};', '}  // namespace ps2']
    open(a.out + '_table.cpp', 'w').write('\n'.join(tab) + '\n')
    print(f'{len(fns)} functions in {files} files + table')


if __name__ == '__main__':
    main()
