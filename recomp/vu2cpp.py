#!/usr/bin/env python3
"""vu2cpp.py - the game's VU microprograms as C++ (static recompilation).

Input: the simulation's program-start dumps (SIM_VU_STATE=N writes
vus<unit>_<entry>_<image>_<k>.bin: the unit's registers and memories at a program
start). For every (unit, entry, micro memory) the code reachable from the entry is
found (branch delay slots, the E bit, BAL / JALR return addresses for JR / JALR), and
programs whose reachable code is identical become one function.

Output: a file included at the end of MGO1Loader/vu0.cpp. Each function is MicroRun's
loop partially evaluated for its instructions: the same cycle count, stalls, flag
slots, Q / P latencies, integer-register backup for branches, lower-reads-upper and
dropped-lower rules, branch delay slots and E bit, with the decoding done here. A
dynamic jump to an address the function does not hold hands the run to the
interpreter's loop with the same state. vu0.cpp checks a program's code (a hash of its
instruction ranges) against the micro memory before running it.

    python3 recomp/vu2cpp.py <dump folder>... -o MGO1Loader/gen [--write-manifest recomp/vu_programs.txt --images <dir>]
    python3 recomp/vu2cpp.py --manifest recomp/vu_programs.txt --images <dir> -o MGO1Loader/gen

The manifest (recomp/vu_programs.txt) says where each program start's code is in the images
tools/ps2_images.py writes (the base image and the page images): offsets only, no code. The
build takes the code from the user's own images through it; the dumps are only needed to
find new programs.
"""
import argparse, glob, os, re, struct, sys

K_FLAG_STATUS = 1 << 16
K_FLAG_CLIP = 1 << 18
VU_SIZE = 16 * 1024 * 2 + (32 * 4 + 4 + 32) * 4     # the Vu0 struct: vf, acc, vi, mem, micro


def fld(f):
    return 8 >> (f & 3)


def fnv(data, h=1469598103934665603):
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


class Regs:
    __slots__ = ('r0', 'f0', 'r1', 'f1', 'wr', 'wf', 'fmac', 'viwrite')

    def __init__(self):
        self.r0 = self.f0 = self.r1 = self.f1 = self.wr = self.wf = 0
        self.fmac = False
        self.viwrite = 0


def upper_regs(w):                      # vu0.cpp UpperRegs
    r = Regs()
    ft, fs, fd, dest = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 21) & 15
    f1 = w & 0x3f
    r.fmac = True
    if f1 < 0x3c:
        r.r0, r.f0, r.wr, r.wf = fs, dest, fd, dest
        if f1 < 0x1c:
            r.r1, r.f1 = ft, fld(w)
        elif f1 >= 0x28:
            r.r1, r.f1 = ft, dest
        if f1 == 0x2e:
            r.f0 = r.f1 = r.wf = 0xe
        return r
    f2 = ((w >> 4) & 0x7c) | (w & 3)
    if f2 == 0x2f:
        r.fmac = False
        return r
    if (0x10 <= f2 <= 0x17) or f2 == 0x1d:
        r.r0, r.f0, r.wr, r.wf = fs, dest, ft, dest
        return r
    if f2 == 0x1f:
        r.r0, r.f0, r.r1, r.f1, r.viwrite = fs, 0xe, ft, 1, K_FLAG_CLIP
        return r
    r.r0, r.f0 = fs, dest
    if f2 < 0x1c:
        r.r1, r.f1 = ft, fld(w)
    elif f2 >= 0x28:
        r.r1, r.f1 = ft, dest
    if f2 == 0x2e:
        r.f0 = r.f1 = 0xe
    return r


class Lower:
    pass


def lower_info(lo):                     # vu0.cpp MicroRun: the lower instruction's registers and pipe
    L = Lower()
    L.lop = lo >> 25
    L.lft, L.lfs, L.ldest = (lo >> 16) & 31, (lo >> 11) & 31, (lo >> 21) & 15
    L.fsf, L.ftf = (lo >> 21) & 3, (lo >> 23) & 3
    L.lf1 = L.lf2 = -1
    L.r = Regs()
    L.fdiv = L.efu = L.branch = L.ialu = False
    L.bread = 0
    lop, r = L.lop, L.r
    if lop == 0x40:
        L.lf1 = lo & 0x3f
        if L.lf1 >= 0x3c:
            L.lf2 = ((lo >> 4) & 0x7c) | (lo & 3)
    if lop == 0x00:
        r.fmac, r.wr, r.wf = True, L.lft, L.ldest
    elif lop == 0x01:
        r.fmac, r.r0, r.f0 = True, L.lfs, L.ldest
    elif lop in (0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x1a, 0x1b, 0x1c):
        r.fmac = True
    elif lop == 0x11:
        r.fmac, r.viwrite = True, K_FLAG_CLIP
    elif lop == 0x15:
        r.fmac, r.viwrite = True, K_FLAG_STATUS
    elif lop == 0x40:
        f2 = L.lf2
        if f2 == 0x30:
            if L.lft:
                r.fmac, r.r0, r.f0, r.wr, r.wf = True, L.lfs, L.ldest, L.lft, L.ldest
        elif f2 == 0x31:
            r.fmac, r.r0, r.f0, r.wr, r.wf = True, L.lfs, ((L.ldest >> 1) | ((L.ldest << 3) & 8)), L.lft, L.ldest
        elif f2 in (0x34, 0x36):
            r.fmac, r.wr, r.wf = True, L.lft, L.ldest
        elif f2 in (0x35, 0x37):
            r.fmac, r.r0, r.f0 = True, L.lfs, L.ldest
        elif f2 in (0x38, 0x3a):
            L.fdiv = True
            r.r0, r.f0, r.r1, r.f1 = L.lfs, fld(L.fsf), L.lft, fld(L.ftf)
        elif f2 == 0x39:
            L.fdiv = True
            r.r1, r.f1 = L.lft, fld(L.ftf)
        elif f2 == 0x3b:
            L.fdiv = True
        elif f2 == 0x3c:
            r.fmac, r.r0, r.f0 = True, L.lfs, fld(L.fsf)
        elif f2 in (0x3d, 0x40, 0x41):
            r.fmac, r.wr, r.wf = True, L.lft, L.ldest
        elif f2 in (0x42, 0x43):
            r.fmac, r.r0, r.f0 = True, L.lfs, fld(L.fsf)
        elif f2 == 0x64:
            r.fmac, r.wr, r.wf = True, L.lft, L.ldest
        elif f2 in (0x70, 0x71, 0x72, 0x73, 0x76):
            L.efu = True
            r.r0, r.f0 = L.lfs, (0xf if f2 == 0x76 else 0xe)
        elif f2 == 0x74:
            L.efu = True
            r.r0, r.f0 = L.lfs, 0xc
        elif f2 == 0x75:
            L.efu = True
            r.r0, r.f0 = L.lfs, 0xa
        elif f2 in (0x78, 0x79, 0x7a, 0x7c, 0x7d, 0x7e):
            L.efu = True
            r.r0, r.f0 = L.lfs, fld(L.fsf)
        elif f2 == 0x7b:
            L.efu = True
    if lop in (0x20, 0x21):
        L.branch = True
    elif lop in (0x24, 0x25):
        L.branch, L.bread = True, 1 << (L.lfs & 15)
    elif lop in (0x28, 0x29):
        L.branch, L.bread = True, (1 << (L.lfs & 15)) | (1 << (L.lft & 15))
    elif lop in (0x2c, 0x2d, 0x2e, 0x2f):
        L.branch, L.bread = True, 1 << (L.lfs & 15)
    elif lop == 0x04:
        L.ialu = True
    elif lop == 0x40 and L.lf2 == 0x3e:
        L.ialu = True
    return L


def imm11(lo):
    v = lo & 0x7ff
    return v - 0x800 if lo & 0x400 else v


# ---- control flow: the addresses a program may run from its entry. A state is (pc, armed,
# ebit) as MicroRun keeps them: armed is a taken branch's target (-1 none, DYN a register's);
# a register's target (JR / JALR) is any return address a BAL / JALR of the program records
DYN = -2


def vi_writes(lo, hi):
    """the integer registers a pair's lower instruction writes (vu0.cpp's lower semantics)"""
    if hi & 0x80000000:
        return set()
    lop = lo >> 25
    lft, lfs = (lo >> 16) & 15, (lo >> 11) & 15
    if lop in (0x04, 0x08, 0x09, 0x14, 0x16, 0x17, 0x18, 0x1a, 0x1b, 0x1c, 0x21, 0x25):
        return {lft}
    if lop in (0x10, 0x12, 0x13):
        return {1}
    if lop == 0x40:
        lf1 = lo & 0x3f
        if lf1 in (0x30, 0x31, 0x34, 0x35):
            return {(lo >> 6) & 15}
        if lf1 == 0x32:
            return {lft}
        if lf1 >= 0x3c:
            lf2 = ((lo >> 4) & 0x7c) | (lo & 3)
            if lf2 in (0x3c, 0x3e, 0x68, 0x69):
                return {lft}
            if lf2 in (0x34, 0x36):
                return {lfs}
            if lf2 in (0x35, 0x37):
                return {lft}
    return set()


def reachable(micro, entry, pcmask):
    """the instructions a program may run from its entry. A state is MicroRun's (pc, armed,
    ebit) with the return address a BAL / JALR left in a register (lreg, lval) when no other
    instruction wrote that register since: a JR / JALR through it goes back there; any
    other register's jump may reach every return address of the program"""
    def word(pc):
        return struct.unpack_from('<II', micro, pc)
    seen, pcs, links = set(), set(), set()
    edges = set()                                       # (pc, next pc) of every transition
    pending = set()                                     # (armed, ebit, lreg, lval) after an unknown register jump
    pend_from = set()
    todo = [(entry & pcmask, -1, 0, -1, 0)]
    while True:
        while todo:
            st = todo.pop()
            if st in seen:
                continue
            seen.add(st)
            pc, armed, ebit, lreg, lval = st
            pcs.add(pc)
            lo, hi = word(pc)
            if hi & 0x40000000:
                ebit = 2
            jumps = [-1]
            nl, nv = lreg, lval                             # the link after this instruction
            if not hi & 0x80000000:
                lop = lo >> 25
                target = (pc + 8 + imm11(lo) * 8) & pcmask
                if lop in (0x20, 0x21):
                    jumps = [target]
                elif lop in (0x28, 0x29, 0x2c, 0x2d, 0x2e, 0x2f):
                    jumps = [target, -1]
                elif lop in (0x24, 0x25):
                    r = (lo >> 11) & 15
                    jumps = [lval if (r == lreg and lreg > 0) else DYN]
                w = vi_writes(lo, hi)
                if nl in w:
                    nl, nv = -1, 0
                if lop in (0x21, 0x25):
                    if armed == DYN:
                        link = None
                    else:
                        link = ((armed if armed >= 0 else pc + 8) + 8) & pcmask
                        links.add(link)
                    if (lo >> 16) & 15:
                        nl, nv = ((lo >> 16) & 15, link) if link is not None else (-1, 0)
            for j in jumps:
                a = armed
                nextpc = pc + 8
                if a >= 0:
                    nextpc = a
                a2 = j                                      # the new armed value (the jump, or none)
                e = ebit
                if e > 0:
                    e -= 1
                    if e + 1 == 1:
                        continue                            # the program ends after this instruction
                if a == DYN:
                    pending.add((a2, e, nl, nv))
                    pend_from.add(pc)
                    for l in links:
                        todo.append((l, a2, e, nl, nv))
                        edges.add((pc, l))
                    continue
                todo.append((nextpc & pcmask, a2, e, nl, nv))
                edges.add((pc, nextpc & pcmask))
        grown = False
        for (a2, e, nl, nv) in list(pending):
            for l in links:
                if (l, a2, e, nl, nv) not in seen:
                    todo.append((l, a2, e, nl, nv))
                    grown = True
        if not grown:
            break
    states = {}
    for (pc, armed, ebit, lreg, lval) in seen:
        states.setdefault(pc, set()).add((armed, ebit))
    for pc in pend_from:
        for l in links:
            edges.add((pc, l))
    reachable.states = states
    reachable.edges = edges
    return sorted(pcs), sorted(links)


def ranges_of(pcs):
    out = []
    for pc in pcs:
        if out and out[-1][1] == pc:
            out[-1][1] = pc + 8
        else:
            out.append([pc, pc + 8])
    return out


# ---- the upper instruction (vu0.cpp Vu0Macro, partially evaluated)
def upper_op(hi, tag):
    w = hi & 0x01ffffff
    ft, fs, fd, dest = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 21) & 15
    f1 = w & 0x3f
    bcx = f'&V.vf[{ft}][{w & 3}]'
    dst = f'Dst({fd})'
    lit = f'Vu0Macro(dummy, 0x{0x4A000000 | w:08x}u, 0x{tag:08x}u);'
    def ar(kind, d, second, bc):
        return f'Arith(Kind::{kind}, {d}, {dest}, V.vf[{fs}], {second}, {bc});'
    def lanes(expr, target):
        if not target:
            return ''
        return ' '.join(f'V.vf[{target}][{l}] = {expr.format(l=l)};' for l in range(4) if dest & (8 >> l))
    if f1 < 0x3c:
        if f1 <= 0x03: return ar('Add', dst, 'nullptr', bcx)
        if f1 <= 0x07: return ar('Sub', dst, 'nullptr', bcx)
        if f1 <= 0x0b: return ar('Madd', dst, 'nullptr', bcx)
        if f1 <= 0x0f: return ar('Msub', dst, 'nullptr', bcx)
        if f1 <= 0x13: return lanes(f'FpMax(V.vf[{fs}][{{l}}], *{bcx})', fd)
        if f1 <= 0x17: return lanes(f'FpMin(V.vf[{fs}][{{l}}], *{bcx})', fd)
        if f1 <= 0x1b: return ar('Mul', dst, 'nullptr', bcx)
        m = {0x1c: ('Mul', 'q'), 0x1e: ('Mul', 'i'), 0x20: ('Add', 'q'), 0x21: ('Madd', 'q'), 0x22: ('Add', 'i'),
             0x23: ('Madd', 'i'), 0x24: ('Sub', 'q'), 0x25: ('Msub', 'q'), 0x26: ('Sub', 'i'), 0x27: ('Msub', 'i')}
        if f1 in m:
            k, s = m[f1]
            return ar(k, dst, 'nullptr', '&V.vi[kViQ]' if s == 'q' else '&V.vi[kViI]')
        if f1 == 0x1d: return lanes(f'FpMax(V.vf[{fs}][{{l}}], V.vi[kViI])', fd)
        if f1 == 0x1f: return lanes(f'FpMin(V.vf[{fs}][{{l}}], V.vi[kViI])', fd)
        m2 = {0x28: 'Add', 0x29: 'Madd', 0x2a: 'Mul', 0x2c: 'Sub', 0x2d: 'Msub'}
        if f1 in m2: return ar(m2[f1], dst, f'V.vf[{ft}]', 'nullptr')
        if f1 == 0x2b: return lanes(f'FpMax(V.vf[{fs}][{{l}}], V.vf[{ft}][{{l}}])', fd)
        if f1 == 0x2f: return lanes(f'FpMin(V.vf[{fs}][{{l}}], V.vf[{ft}][{{l}}])', fd)
        return lit                                                  # OPMSUB and the integer / call forms: as the interpreter
    f2 = ((w >> 4) & 0x7c) | (w & 3)
    bc2 = f'&V.vf[{ft}][{w & 3}]'
    if f2 <= 0x03: return ar('Add', 'V.acc', 'nullptr', bc2)
    if f2 <= 0x07: return ar('Sub', 'V.acc', 'nullptr', bc2)
    if f2 <= 0x0b: return ar('Madd', 'V.acc', 'nullptr', bc2)
    if f2 <= 0x0f: return ar('Msub', 'V.acc', 'nullptr', bc2)
    if f2 <= 0x13: return lanes(f'ItoF(V.vf[{fs}][{{l}}], {(0, 4, 12, 15)[f2 & 3]})', ft)
    if f2 <= 0x17: return lanes(f'FtoI(V.vf[{fs}][{{l}}], {(0, 4, 12, 15)[f2 & 3]})', ft)
    if f2 <= 0x1b: return ar('Mul', 'V.acc', 'nullptr', bc2)
    m = {0x1c: ('Mul', 'q'), 0x1e: ('Mul', 'i'), 0x20: ('Add', 'q'), 0x21: ('Madd', 'q'), 0x22: ('Add', 'i'),
         0x23: ('Madd', 'i'), 0x24: ('Sub', 'q'), 0x25: ('Msub', 'q'), 0x26: ('Sub', 'i'), 0x27: ('Msub', 'i')}
    if f2 in m:
        k, s = m[f2]
        return ar(k, 'V.acc', 'nullptr', '&V.vi[kViQ]' if s == 'q' else '&V.vi[kViI]')
    if f2 == 0x1d: return lanes(f'V.vf[{fs}][{{l}}] & 0x7fffffffu', ft)
    m2 = {0x28: 'Add', 0x29: 'Madd', 0x2a: 'Mul', 0x2c: 'Sub', 0x2d: 'Msub'}
    if f2 in m2: return ar(m2[f2], 'V.acc', f'V.vf[{ft}]', 'nullptr')
    if f2 == 0x2f: return ''                                        # NOP
    return lit                                                      # CLIP, OPMULA and the rest: as the interpreter


def upper_writes_flags(hi):
    """whether the upper operation may change MAC / status / clip (else no flag exchange is needed)"""
    w = hi & 0x01ffffff
    f1 = w & 0x3f
    if f1 < 0x3c:
        return not (0x10 <= f1 <= 0x17 or f1 in (0x1d, 0x1f, 0x2b, 0x2f))
    f2 = ((w >> 4) & 0x7c) | (w & 3)
    return not (0x10 <= f2 <= 0x17 or f2 in (0x1d, 0x2f))


# ---- the lower instruction (vu0.cpp MicroRun's switch)
def lower_op(lo, L, pc, pcmask, qmask, unit, tag):
    lop, lft, lfs, ldest, fsf = L.lop, L.lft, L.lfs, L.ldest, L.fsf
    i11 = imm11(lo)
    nxt = (pc + 8 + i11 * 8) & pcmask
    link = f'((((armed >= 0 ? (uint32_t)armed : {pc + 8}u) + 8) >> 3) & 0xffff)'
    memq = lambda e: f'(V.mem + ((({e}) & {qmask}u) << 4))' if unit else f'MemQ({e})'
    c = []
    bk = []                                                        # a backed-up integer register: its number
    if lop == 0x00:
        c.append(f'{{ const uint8_t* m = {memq(f"(uint32_t)((int16_t)VIr({lfs}) + {i11})")};')
        if lft:
            c += [f'memcpy(&V.vf[{lft}][{l}], m + {4 * l}, 4);' for l in range(4) if ldest & (8 >> l)]
        c.append('(void)m; }')
    elif lop == 0x01:
        c.append(f'{{ uint8_t* m = {memq(f"(uint32_t)((int16_t)VIr({lft}) + {i11})")};')
        c += [f'memcpy(m + {4 * l}, &V.vf[{lfs}][{l}], 4);' for l in range(4) if ldest & (8 >> l)]
        c.append('(void)m; }')
    elif lop == 0x04:
        c.append(f'{{ const uint8_t* m = {memq(f"(uint32_t)((int16_t)VIr({lfs}) + {i11})")};')
        if lft & 15:
            c += [f'{{ uint16_t h; memcpy(&h, m + {4 * l}, 2); VIr({lft}) = h; }}' for l in range(4) if ldest & (8 >> l)]
        c.append('(void)m; }')
    elif lop == 0x05:
        c.append(f'{{ uint8_t* m = {memq(f"(uint32_t)((int16_t)VIr({lfs}) + {i11})")}; const uint32_t v = VIr({lft}) & 0xffff;')
        c += [f'memcpy(m + {4 * l}, &v, 4);' for l in range(4) if ldest & (8 >> l)]
        c.append('(void)m; (void)v; }')
    elif lop in (0x08, 0x09):
        i15 = ((lo >> 10) & 0x7800) | (lo & 0x7ff)
        if lft & 15:
            bk.append(lft)
            op = '+' if lop == 0x08 else '-'
            c.append(f'VIr({lft}) = (VIr({lfs}) {op} {i15}u) & 0xffff;')
    elif lop == 0x10: c.append(f'VIr(1) = ((V.vi[kViClip] & 0xffffff) == {lo & 0xffffff}u) ? 1 : 0;')
    elif lop == 0x11: c.append(f'gMs.clip = {lo & 0xffffff}u;')
    elif lop == 0x12: c.append(f'VIr(1) = (V.vi[kViClip] & {lo & 0xffffff}u) ? 1 : 0;')
    elif lop == 0x13: c.append(f'VIr(1) = (((V.vi[kViClip] | {lo & 0xffffff}u) & 0xffffff) == 0xffffff) ? 1 : 0;')
    elif lop in (0x14, 0x15, 0x16, 0x17):
        i12 = ((lo >> 10) & 0x800) | (lo & 0x7ff)
        if lop == 0x15:
            c.append(f'gMs.status = ({i12 & 0xfc0}u) | (gMs.status & 0x3f);')
        elif lft & 15:
            if lop == 0x14: c.append(f'VIr({lft}) = ((V.vi[kViStatus] & 0xfff) == {i12}u) ? 1 : 0;')
            elif lop == 0x16: c.append(f'VIr({lft}) = V.vi[kViStatus] & {i12}u & 0xfff;')
            else: c.append(f'VIr({lft}) = (V.vi[kViStatus] | {i12}u) & 0xfff;')
    elif lop == 0x18:
        if lft & 15: c.append(f'VIr({lft}) = ((V.vi[kViMac] & 0xffff) == (VIr({lfs}) & 0xffff)) ? 1 : 0;')
    elif lop == 0x1a:
        if lft & 15: c.append(f'VIr({lft}) = V.vi[kViMac] & VIr({lfs}) & 0xffff;')
    elif lop == 0x1b:
        if lft & 15: c.append(f'VIr({lft}) = (V.vi[kViMac] | VIr({lfs})) & 0xffff;')
    elif lop == 0x1c:
        if lft & 15: c.append(f'VIr({lft}) = V.vi[kViClip] & 0xfff;')
    elif lop == 0x20: c.append(f'jump = {nxt};')
    elif lop == 0x21:
        if lft & 15: c.append(f'VIr({lft}) = {link};')
        c.append(f'jump = {nxt};')
    elif lop == 0x24: c.append(f'jump = ((VIr({lfs}) & 0xffff) * 8) & {pcmask};')
    elif lop == 0x25:
        c.append(f'{{ const uint32_t t = ((VIr({lfs}) & 0xffff) * 8) & {pcmask};')
        if lft & 15: c.append(f'VIr({lft}) = {link};')
        c.append('jump = t; }')
    elif lop in (0x28, 0x29, 0x2c, 0x2d, 0x2e, 0x2f):
        cond = {0x28: f'BR({lft}) == BR({lfs})', 0x29: f'BR({lft}) != BR({lfs})', 0x2c: f'BR({lfs}) < 0',
                0x2d: f'BR({lfs}) > 0', 0x2e: f'BR({lfs}) <= 0', 0x2f: f'BR({lfs}) >= 0'}[lop]
        c.append(f'if ({cond}) jump = {nxt};')
    elif lop == 0x40:
        lf1, lf2 = L.lf1, L.lf2
        word = 0x4A000000 | (lo & 0x01ffffff)
        if lf2 in (0x38, 0x39, 0x3a):
            c.append('{ const uint32_t q0 = V.vi[kViQ], st0 = V.vi[kViStatus]; V.vi[kViStatus] = gMs.status;')
            c.append(f'Vu0Macro(dummy, 0x{word:08x}u, 0x{tag | 4:08x}u);')
            c.append('gMs.qVal = V.vi[kViQ]; gMs.status = V.vi[kViStatus]; gMs.qStat = gMs.status;')
            c.append('V.vi[kViQ] = q0; V.vi[kViStatus] = st0;')
            c.append(f'gMs.qBusy = true; gMs.qStart = gMs.cycle; gMs.qCycles = {13 if lf2 == 0x3a else 7}; }}')
        elif lf2 == 0x3b:
            pass
        elif lf2 >= 0x64:
            if not unit:
                c.append(f'Once("VU1 instruction in a VU0 microprogram (ignored)", 0x{tag:08x}u, 0x{lo:08x}u);')
            else:
                c.append(f'Vu1Lower({lf2}, 0x{lo:08x}u, {lft}, {lfs}, {ldest}, {fsf}, 0x{tag:08x}u);')
        else:
            if lf1 in (0x30, 0x31, 0x34, 0x35):
                if (lo >> 6) & 15: bk.append((lo >> 6) & 15)
            elif lf1 == 0x32 or lf2 == 0x3c:
                if lft & 15: bk.append(lft)
            elif lf2 in (0x34, 0x36):
                bk.append(lfs)
            elif lf2 in (0x35, 0x37):
                bk.append(lft)
            c.append(lower_generic(lo, lf1, lf2, word, tag, memq))
    else:
        c.append(f'Once("VU0 micro lower instruction not implemented", 0x{tag:08x}u, 0x{lo:08x}u);')
    return c, bk


def lower_generic(lo, lf1, lf2, word, tag, memq):
    """the lower instructions MicroRun hands to Vu0Macro, partially evaluated"""
    ft, fs, fd, dest = (lo >> 16) & 31, (lo >> 11) & 31, (lo >> 6) & 31, (lo >> 21) & 15
    fsf = (lo >> 21) & 3
    lanes = [l for l in range(4) if dest & (8 >> l)]
    if lf1 == 0x30:
        return f'VIr({fd}) = (uint16_t)((int16_t)VIr({fs}) + (int16_t)VIr({ft}));' if fd & 15 else ''
    if lf1 == 0x31:
        return f'VIr({fd}) = (uint16_t)((int16_t)VIr({fs}) - (int16_t)VIr({ft}));' if fd & 15 else ''
    if lf1 == 0x32:
        imm = (0xfff0 | (fd & 0xf)) if fd & 0x10 else (fd & 0xf)
        imm = imm - 0x10000 if imm & 0x8000 else imm
        return f'VIr({ft}) = (uint16_t)((int16_t)VIr({fs}) + ({imm}));' if ft & 15 else ''
    if lf1 == 0x34:
        return f'VIr({fd}) = VIr({fs}) & VIr({ft}) & 0xffff;' if fd & 15 else ''
    if lf1 == 0x35:
        return f'VIr({fd}) = (VIr({fs}) | VIr({ft})) & 0xffff;' if fd & 15 else ''
    if lf1 < 0x3c:
        return f'Vu0Macro(dummy, 0x{word:08x}u, 0x{tag | 4:08x}u);'
    if lf2 == 0x30:
        return ' '.join(f'V.vf[{ft}][{l}] = V.vf[{fs}][{l}];' for l in lanes) if ft else ''
    if lf2 == 0x31:
        if not ft:
            return ''
        return '{ uint32_t s[4]; memcpy(s, V.vf[%d], 16); %s }' % (fs, ' '.join(f'V.vf[{ft}][{l}] = s[{(l + 1) & 3}];' for l in lanes))
    if lf2 == 0x34:                                               # LQI ft, (is++)
        a = f'{{ const uint8_t* m = Mem(VIr({fs}) * 16); ' + ' '.join(f'memcpy(&V.vf[{ft}][{l}], m + {4 * l}, 4);' for l in lanes) + ' }' if ft else ''
        b = f' VIr({fs}) = (VIr({fs}) + 1) & 0xffff;' if fs & 15 else ''
        return a + b
    if lf2 == 0x35:                                               # SQI fs, (it++)
        a = f'{{ uint8_t* m = Mem(VIr({ft}) * 16); ' + ' '.join(f'memcpy(m + {4 * l}, &V.vf[{fs}][{l}], 4);' for l in lanes) + ' (void)m; }'
        b = f' VIr({ft}) = (VIr({ft}) + 1) & 0xffff;' if ft & 15 else ''
        return a + b
    if lf2 == 0x36:                                               # LQD ft, (--is)
        a = f'VIr({fs}) = (VIr({fs}) - 1) & 0xffff; ' if fs & 15 else ''
        b = f'{{ const uint8_t* m = Mem(VIr({fs}) * 16); ' + ' '.join(f'memcpy(&V.vf[{ft}][{l}], m + {4 * l}, 4);' for l in lanes) + ' }' if ft else ''
        return a + b
    if lf2 == 0x37:                                               # SQD fs, (--it)
        a = f'VIr({ft}) = (VIr({ft}) - 1) & 0xffff; ' if ft & 15 else ''
        b = f'{{ uint8_t* m = Mem(VIr({ft}) * 16); ' + ' '.join(f'memcpy(m + {4 * l}, &V.vf[{fs}][{l}], 4);' for l in lanes) + ' (void)m; }'
        return a + b
    if lf2 == 0x3c:
        return f'VIr({ft}) = V.vf[{fs}][{fsf}] & 0xffff;' if ft & 15 else ''
    if lf2 == 0x3d:
        return ' '.join(f'V.vf[{ft}][{l}] = (uint32_t)(int32_t)(int16_t)VIr({fs});' for l in lanes) if ft else ''
    return f'Vu0Macro(dummy, 0x{word:08x}u, 0x{tag | 4:08x}u);'


def gen_program(name, unit, micro, pcs, entries):
    pcmask = 0x3ff8 if unit else 0xff8
    qmask = 0x3ff if unit else 0xff
    pcset = set(pcs)
    out = [f'// {name}: VU{unit}, entries {" ".join("%04x" % e for e in entries)}, {len(pcs)} instructions',
           f'void {name}(MicroPos& P) {{',
           '    Ctx dummy{}; (void)dummy;',
           '    uint32_t pc = P.pc; int64_t jump = P.jump, armed = P.armed; int ebit = P.ebit; long steps = P.steps;',
           '    auto BR = [&](int r) -> int16_t { r &= 15; if (gMs.bkLeft > 0 && gMs.bkReg == r) return (int16_t)gMs.bkVal; return r ? (int16_t)VIr(r) : 0; };',
           '    (void)BR;',
           'dispatch:',
           '    switch (pc) {']
    for pc in pcs:
        out.append(f'    case {pc}: goto L{pc:04x};')
    out += ['    default:',
            '        P.pc = pc; P.jump = jump; P.armed = armed; P.ebit = ebit; P.steps = steps;',
            '        MicroLoop(P);                                  // a target this program does not hold: the interpreter goes on',
            '        return;',
            '    }']
    for pc in pcs:
        lo, hi = struct.unpack_from('<II', micro, pc)
        tag = 0xF0000000 | pc
        out.append(f'L{pc:04x}: {{                                   // {hi:08x} {lo:08x}')
        body = []
        body.append('++gMs.cycle;')
        if hi & 0x40000000:
            body.append('ebit = 2;')
        imm = (hi & 0x80000000) != 0
        ur = upper_regs(hi)
        body.append('const int64_t before = gMs.cycle - 1;')
        if ur.r0 and ur.f0: body.append(f'Stall({ur.r0}, {ur.f0});')
        if ur.r1 and ur.f1: body.append(f'Stall({ur.r1}, {ur.f1});')
        L = None
        lr = Regs()
        if not imm:
            L = lower_info(lo)
            lr = L.r
            if lr.r0 and lr.f0: body.append(f'Stall({lr.r0}, {lr.f0});')
            if lr.r1 and lr.f1: body.append(f'Stall({lr.r1}, {lr.f1});')
            if L.branch: body.append(f'StallIalu({L.bread}u);')
            if L.fdiv: body.append('if (gMs.qBusy && gMs.qStart + gMs.qCycles > gMs.cycle) gMs.cycle = gMs.qStart + gMs.qCycles;')
            if L.efu: body.append('if (gEfu.busy && gEfu.start + gEfu.cycles > gMs.cycle) gMs.cycle = gEfu.start + gEfu.cycles;')
        body.append('TestPipes();')
        body.append('if (gMs.bkLeft > 0) gMs.bkLeft -= (int)std::min<int64_t>(gMs.cycle - before, gMs.bkLeft);')
        lower_reads_upper = (not imm) and ur.wr > 0 and (lr.r0 == ur.wr or lr.r1 == ur.wr)
        lower_dropped = (not imm) and ((ur.wr > 0 and lr.wr == ur.wr) or ((ur.viwrite & K_FLAG_CLIP) and L.lop == 0x11))
        if lower_reads_upper:
            body.append(f'uint32_t oldVf[4]; memcpy(oldVf, V.vf[{ur.wr}], 16);')
        up = upper_op(hi, tag)
        if upper_writes_flags(hi):
            body.append('{ const uint32_t macVis = V.vi[kViMac], stVis = V.vi[kViStatus], clipVis = V.vi[kViClip];')
            body.append('V.vi[kViMac] = gMs.mac; V.vi[kViStatus] = gMs.status; V.vi[kViClip] = gMs.clip;')
            body.append(up)
            body.append('gMs.mac = V.vi[kViMac]; gMs.status = V.vi[kViStatus]; gMs.clip = V.vi[kViClip];')
            body.append('V.vi[kViMac] = macVis; V.vi[kViStatus] = stVis; V.vi[kViClip] = clipVis; }')
        elif up:
            body.append(up)
        if imm:
            body.append(f'V.vi[kViI] = 0x{lo:08x}u;')
        elif not lower_dropped:
            if lower_reads_upper:
                body.append(f'uint32_t newVf[4]; memcpy(newVf, V.vf[{ur.wr}], 16); memcpy(V.vf[{ur.wr}], oldVf, 16);')
            code, bk = lower_op(lo, L, pc, pcmask, qmask, unit, tag)
            if bk:
                r = bk[-1] & 15
                body.append(f'const uint32_t viOld = VIr({r}) & 0xffff;')
            body += code
            if lower_reads_upper:
                body.append(f'memcpy(V.vf[{ur.wr}], newVf, 16);')
            if bk:
                r = bk[-1] & 15
                body.append(f'if (!(gMs.bkLeft > 0 && gMs.bkReg == {r})) {{ gMs.bkReg = {r}; gMs.bkVal = viOld; }}')
                body.append('gMs.bkLeft = 2;')
        if L is not None and L.ialu:
            body.append('if (gMs.nialu == 4) { memmove(gMs.ialu, gMs.ialu + 1, 3 * sizeof(gMs.ialu[0])); --gMs.nialu; }')
            body.append(f'gMs.ialu[gMs.nialu++] = {{gMs.cycle, {1 << (L.lft & 15)}u}};')
        lf = lr.fmac and not imm
        if ur.fmac or lf:
            body.append('{ if (gMs.nslots == 8) { CompleteSlot(gMs.slot[0]); memmove(gMs.slot, gMs.slot + 1, 7 * sizeof(Slot)); --gMs.nslots; }')
            body.append('Slot& p = gMs.slot[gMs.nslots++]; p = Slot{}; p.start = gMs.cycle;')
            if ur.fmac:
                body.append(f'p.regU = {ur.wr}; p.fieldsU = {ur.wf}; p.flagreg = {ur.viwrite}u;')
            if lf:
                body.append(f'p.regL = {lr.wr}; p.fieldsL = {lr.wf}; p.flagreg |= {lr.viwrite}u;')
            body.append('p.mac = gMs.mac; p.status = gMs.status; p.clip = gMs.clip; }')
        nxt = (pc + 8) & pcmask
        body.append(f'uint32_t nextPc = {pc + 8}u;')
        body.append('if (armed >= 0) { nextPc = (uint32_t)armed; armed = -1; }')
        body.append('if (jump >= 0) { armed = jump; jump = -1; }')
        body.append(f'pc = nextPc & {pcmask}u;')
        body.append('if (ebit > 0 && ebit-- == 1) goto done;')
        body.append('if (++steps >= gVuStepLimit) goto done;')
        if nxt in pcset:
            body.append(f'if (pc == {nxt}u) goto L{nxt:04x};')
        body.append('goto dispatch;')
        out += ['    ' + b for b in body]
        out.append('}')
    out += ['done:',
            '    P.pc = pc; P.jump = jump; P.armed = armed; P.ebit = ebit; P.steps = steps;',
            '}', '']
    return out



# ---- the fast form: a VU1 program when no VU1 program reads the status flags (FSEQ / FSAND /
# FSOR). The cycle count, stalls, Q / P latencies, MAC and clip completions, integer-load
# stalls and integer-register backups are MicroRun's, kept in locals: the ready cycle of each
# VF field the program writes, the pending Q, the pending MAC and clip values (issue side, then
# visible 4 cycles later); the status flags are not computed.
FLAG_READS = (0x14, 0x16, 0x17)
FAST_UPPER_LIT_OK = set()


def upper_fast(hi, tag):
    """the upper operation without flags, or None when this form does not cover it"""
    w = hi & 0x01ffffff
    ft, fs, fd, dest = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 21) & 15
    f1 = w & 0x3f
    if f1 < 0x3c:
        if f1 == 0x2e:                                      # OPMSUB (xyz): fd = ACC - fs x ft
            return ('{ uint32_t* d = Dst(%d); const float tx = Op(V.vf[%d][0]), ty = Op(V.vf[%d][1]), tz = Op(V.vf[%d][2]);'
                    ' const float sx = Op(V.vf[%d][0]), sy = Op(V.vf[%d][1]), sz = Op(V.vf[%d][2]);'
                    ' uint32_t acc[4]; memcpy(acc, V.acc, 16);'
                    ' d[0] = MacR(imac, 0, Op(acc[0]) - sy * tz); d[1] = MacR(imac, 1, Op(acc[1]) - sz * tx); d[2] = MacR(imac, 2, Op(acc[2]) - sx * ty); }'
                    % (fd, ft, ft, ft, fs, fs, fs))
        if 0x30 <= f1:
            return None
        c = upper_op(hi, tag)
        return c.replace('Arith(', 'ArithM(imac, ')
    f2 = ((w >> 4) & 0x7c) | (w & 3)
    if f2 == 0x1f:                                          # CLIPw.xyz on the issue-side clip flags
        return ('{ int32_t v = (int32_t)V.vf[%d][3]; v = (v & 0x7f800000) ? (v & 0x7fffffff) : 0x007fffff; uint32_t cf = iclip << 6;'
                ' for (int l = 0; l < 3; ++l) { if ((int32_t)(V.vf[%d][l] ^ 0x00000000) > v) cf |= 1u << (2 * l);'
                ' if ((int32_t)(V.vf[%d][l] ^ 0x80000000) > v) cf |= 2u << (2 * l); } iclip = cf & 0xffffff; }' % (ft, fs, fs))
    if f2 == 0x2e:                                          # OPMULA (xyz)
        return ('{ const float sx = Op(V.vf[%d][0]), sy = Op(V.vf[%d][1]), sz = Op(V.vf[%d][2]);'
                ' const float tx = Op(V.vf[%d][0]), ty = Op(V.vf[%d][1]), tz = Op(V.vf[%d][2]);'
                ' V.acc[0] = MacR(imac, 0, sy * tz); V.acc[1] = MacR(imac, 1, sz * tx); V.acc[2] = MacR(imac, 2, sx * ty); }' % (fs, fs, fs, ft, ft, ft))
    if f2 >= 0x30 and f2 != 0x2f:
        return None
    c = upper_op(hi, tag)
    if c.startswith('Vu0Macro'):
        return None
    return c.replace('Arith(', 'ArithM(imac, ')


KIND = {'Add': 0, 'Sub': 1, 'Mul': 2, 'Madd': 3, 'Msub': 4}


def upper_simd(hi, keep_mac):
    """the Arith family on the four lanes at once (vu_micro.h ArithV), or None for the others"""
    w = hi & 0x01ffffff
    ft, fs, fd, dest = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 21) & 15
    f1 = w & 0x3f
    q, i = 'BcastOpV(gVu1.vi[kViQ])', 'BcastOpV(gVu1.vi[kViI])'
    bc = f'BcastOpV(gVu1.vf[{ft}][{w & 3}])'
    vec = f'LoadOpV(gVu1.vf[{ft}])'
    table = {}
    for k in range(4):
        table[0x00 + k] = ('Add', bc); table[0x04 + k] = ('Sub', bc); table[0x08 + k] = ('Madd', bc)
        table[0x0c + k] = ('Msub', bc); table[0x18 + k] = ('Mul', bc)
    table.update({0x1c: ('Mul', q), 0x1e: ('Mul', i), 0x20: ('Add', q), 0x21: ('Madd', q), 0x22: ('Add', i), 0x23: ('Madd', i),
                  0x24: ('Sub', q), 0x25: ('Msub', q), 0x26: ('Sub', i), 0x27: ('Msub', i),
                  0x28: ('Add', vec), 0x29: ('Madd', vec), 0x2a: ('Mul', vec), 0x2c: ('Sub', vec), 0x2d: ('Msub', vec)})
    if f1 < 0x3c:
        if f1 not in table:
            return None
        k, b = table[f1]
        d = f'gVu1.vf[{fd}]' if fd else 'nullptr'
    else:
        f2 = ((w >> 4) & 0x7c) | (w & 3)
        if f2 not in table or f2 in (0x1d, 0x1f):
            return None
        k, b = table[f2]
        d = 'gVu1.acc'
    return f'ArithV<{KIND[k]}, {dest}, {"true" if keep_mac else "false"}>({d}, gVu1.vf[{fs}], {b}, imac);'


def upper_simd_clean(hi, keep_mac, clean):
    """upper_simd with the operands vu2cpp knows clean (no exponent 0 nor 255: Op() leaves them as they are) read
    without Op()'s clamp: clean(reg, lane) for VF registers, reg 'acc', 'q', 'i'"""
    w = hi & 0x01ffffff
    ft, fs, fd, dest = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 21) & 15
    f1 = w & 0x3f
    lanes = fields(dest)
    vclean = lambda r: r == 0 or all(clean(r, l) for l in lanes)
    q = 'BcastV(gVu1.vi[kViQ])' if clean('q', 0) else 'BcastOpV(gVu1.vi[kViQ])'
    i = 'BcastV(gVu1.vi[kViI])' if clean('i', 0) else 'BcastOpV(gVu1.vi[kViI])'
    bc = f'BcastV(gVu1.vf[{ft}][{w & 3}])' if (ft == 0 or clean(ft, w & 3)) else f'BcastOpV(gVu1.vf[{ft}][{w & 3}])'
    vec = f'LoadV(gVu1.vf[{ft}])' if vclean(ft) else f'LoadOpV(gVu1.vf[{ft}])'
    table = {}
    for k in range(4):
        table[0x00 + k] = ('Add', bc); table[0x04 + k] = ('Sub', bc); table[0x08 + k] = ('Madd', bc)
        table[0x0c + k] = ('Msub', bc); table[0x18 + k] = ('Mul', bc)
    table.update({0x1c: ('Mul', q), 0x1e: ('Mul', i), 0x20: ('Add', q), 0x21: ('Madd', q), 0x22: ('Add', i), 0x23: ('Madd', i),
                  0x24: ('Sub', q), 0x25: ('Msub', q), 0x26: ('Sub', i), 0x27: ('Msub', i),
                  0x28: ('Add', vec), 0x29: ('Madd', vec), 0x2a: ('Mul', vec), 0x2c: ('Sub', vec), 0x2d: ('Msub', vec)})
    if f1 < 0x3c:
        if f1 not in table:
            return None
        k, b = table[f1]
        d = f'gVu1.vf[{fd}]' if fd else 'nullptr'
    else:
        f2 = ((w >> 4) & 0x7c) | (w & 3)
        if f2 not in table or f2 in (0x1d, 0x1f):
            return None
        k, b = table[f2]
        d = 'gVu1.acc'
    ca = 'true' if vclean(fs) else 'false'
    cc = 'true' if all(clean('acc', l) for l in lanes) else 'false'
    return f'ArithVC<{KIND[k]}, {dest}, {"true" if keep_mac else "false"}, {ca}, {cc}>({d}, gVu1.vf[{fs}], {b}, imac);'


def clean_after(hi, lo, imm, L, lower_dropped, cin):
    """the clean operands (vu2cpp's bit set: VF r lane l at 4r + l, ACC 128 +, I 132, Q 133) after an instruction"""
    VF = lambda r, l: 1 << (4 * r + l)
    ACC = lambda l: 1 << (128 + l)
    I_BIT, Q_BIT = 1 << 132, 1 << 133
    isc = lambda bit: (cin & bit) != 0
    c = cin
    w = hi & 0x01ffffff
    ft, fs, fd, dest = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, (w >> 21) & 15
    f1 = w & 0x3f
    f2 = ((w >> 4) & 0x7c) | (w & 3)
    def setv(r, ls, ok):
        nonlocal c
        if r == 0:
            return
        for l in ls:
            c = (c | VF(r, l)) if ok(l) else (c & ~VF(r, l))
    arith = set(range(0x00, 0x10)) | set(range(0x18, 0x1c)) | {0x1c, 0x1e, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2c, 0x2d}
    lanes = fields(dest)
    if f1 < 0x3c:
        if f1 in arith:
            setv(fd, lanes, lambda l: True)
        elif 0x10 <= f1 <= 0x17:                                 # MAX / MIN bc: one of two operands
            setv(fd, lanes, lambda l: isc(VF(fs, l)) and (ft == 0 or isc(VF(ft, w & 3))))
        elif f1 in (0x1d, 0x1f):                                 # MAXi / MINi
            setv(fd, lanes, lambda l: isc(VF(fs, l)) and isc(I_BIT))
        elif f1 in (0x2b, 0x2f):                                 # MAX / MIN
            setv(fd, lanes, lambda l: isc(VF(fs, l)) and isc(VF(ft, l)))
        elif f1 == 0x2e:                                         # OPMSUB: x y z
            setv(fd, [0, 1, 2], lambda l: True)
        else:
            setv(fd, [0, 1, 2, 3], lambda l: False)
    else:
        if f2 in arith:
            for l in lanes:
                c |= ACC(l)
        elif 0x10 <= f2 <= 0x13:                                 # ITOF
            setv(ft, lanes, lambda l: True)
        elif 0x14 <= f2 <= 0x17:                                 # FTOI
            setv(ft, lanes, lambda l: False)
        elif f2 == 0x1d:                                         # ABS
            setv(ft, lanes, lambda l: isc(VF(fs, l)))
        elif f2 == 0x2e:                                         # OPMULA
            for l in (0, 1, 2):
                c |= ACC(l)
        elif f2 in (0x1f, 0x2f):                                 # CLIP, NOP
            pass
        else:
            c &= ~(VF(ft, 0) | VF(ft, 1) | VF(ft, 2) | VF(ft, 3)) if ft else c
            c &= ~(ACC(0) | ACC(1) | ACC(2) | ACC(3))
    if imm:
        u = lo & 0xffffffff
        e = u & 0x7f800000
        ok = (e != 0 and e != 0x7f800000) or (u & 0x7fffffff) == 0
        c = (c | I_BIT) if ok else (c & ~I_BIT)
    elif not lower_dropped and L is not None:
        lft, lfs, ldest = L.lft, L.lfs, L.ldest
        ll = fields(ldest)
        lop = L.lop
        if lop == 0x00:
            setv(lft, ll, lambda l: False)
        elif lop == 0x40:
            if L.lf2 == 0x30:                                    # MOVE: the old value's (lower reads before upper)
                setv(lft, ll, lambda l: isc(VF(lfs, l)))
            elif L.lf2 == 0x31:                                  # MR32
                setv(lft, ll, lambda l: isc(VF(lfs, (l + 1) & 3)))
            elif L.lf2 in (0x34, 0x36, 0x3d, 0x40, 0x41, 0x64):
                setv(lft, ll, lambda l: False)
    return c


def fast_ok(micro, pcs):
    for pc in pcs:
        lo, hi = struct.unpack_from('<II', micro, pc)
        if upper_fast(hi, 0) is None:
            return False
        if not hi & 0x80000000 and (lo >> 25) in FLAG_READS:
            return False
    return True


def fast_ok_status(micro, pcs):
    """(v141-host beta 13) fast_ok with the status reads (FSEQ FSAND FSOR): VU0's static form with the status kept"""
    for pc in pcs:
        lo, hi = struct.unpack_from('<II', micro, pc)
        if upper_fast(hi, 0) is None:
            return False
    return True


def fields(mask):
    return [l for l in range(4) if mask & (8 >> l)]


def to_vu1(line):
    """emitted code on VU1 by name: its registers are plain addresses"""
    line = re.sub(r'\bV\.', 'gVu1.', line)
    return line.replace('VIr(', 'VIr1(').replace('Dst(', 'Dst1(').replace('ArithM(imac, ', 'ArithM1(imac, ')


def to_vu1s(line):
    """the static form's: also its own EFU and lower helpers (it runs without the micro lock, vu0.cpp Vu1RunStatic)"""
    line = to_vu1(line)
    line = re.sub(r'\bCompleteP\(\)', 'CompleteP1()', line)
    line = re.sub(r'\bVu1Lower\(', 'Vu1Lower1(', line)
    return re.sub(r'\bMem\(', 'Mem1(', line)


def to_vu0s(line):
    """the static form on VU0 (vu0.cpp VuRunCompiled, under the micro lock): VU1's names turned into VU0's (4 KB
    memories, no EFU)"""
    line = to_vu1s(line)
    assert 'CompleteP1' not in line and 'Vu1Lower' not in line and 'Vu1HandOver' not in line and 'gEfu' not in line, line
    for a, b in (('gVu1', 'gVu0'), ('VIr1(', 'VIr0('), ('Dst1(', 'Dst0('), ('ArithM1(', 'ArithM0('), ('ArithV1(', 'ArithV0('),
                 ('ArithVC<', 'ArithVC0<'), ('ArithV<', 'ArithV0<'), ('Mem1(', 'Mem0('), ('MemQ(', 'MemQ0(')):
        line = line.replace(a, b)
    return line


def gen_program_fast(name, micro, pcs, entries, states, keep_mac):
    pcmask = 0x3ff8
    pcset = set(pcs)
    info = {}
    written, loaded = set(), set()
    bread_all = 0
    reads_mac = False
    for pc in pcs:
        lo, hi = struct.unpack_from('<II', micro, pc)
        imm = (hi & 0x80000000) != 0
        ur = upper_regs(hi)
        L = None if imm else lower_info(lo)
        lr = Regs() if imm else L.r
        info[pc] = (lo, hi, imm, ur, L, lr)
        if ur.fmac and ur.wr:
            written |= {(ur.wr, f) for f in fields(ur.wf)}
        if not imm and lr.fmac and lr.wr:
            written |= {(lr.wr, f) for f in fields(lr.wf)}
        if L is not None:
            if L.ialu:
                loaded.add(L.lft & 15)
            if L.branch:
                bread_all |= L.bread
            if L.lop in (0x18, 0x1a, 0x1b):
                reads_mac = True
    keep_mac = keep_mac or reads_mac
    uses_bk = any(L is not None and L.lop in (0x28, 0x29, 0x2c, 0x2d, 0x2e, 0x2f) for (_, _, _, _, L, _) in info.values())
    ialu_regs = sorted(r for r in loaded if bread_all & (1 << r))
    def rv(r, f):
        return f'r{r}{"xyzw"[f]}'
    out = [f'// {name}: VU1 (fast form{"" if keep_mac else ", no MAC"}), entries {" ".join("%04x" % e for e in entries)}, {len(pcs)} instructions',
           f'void {name}(MicroPos& P) {{',
           '    Ctx dummy{}; (void)dummy;',
           '    uint32_t pc = P.pc; int64_t jump = P.jump, armed = P.armed; int ebit = P.ebit; long steps = P.steps;',
           '    int64_t cyc = gMs.cycle;',
           '    const long limit = gVuStepLimit;']
    if written:
        out.append('    int64_t ' + ', '.join(f'{rv(r, f)} = 0' for r, f in sorted(written)) + ';')
    if ialu_regs:
        out.append('    int64_t ' + ', '.join(f'ia{r} = 0' for r in ialu_regs) + ';')
    out += ['    bool qB = false; int64_t qS = 0, qC = 0; uint32_t qV = 0;',
            '    uint32_t iclip = gVu1.vi[kViClip];',
            '    const uint32_t cq0 = gVu1.vi[kViClip]; unsigned cqn = 0; int64_t cqs[8]; uint32_t cqv[8];   // clip: visible at the start, then pending',
            '    const uint32_t mac0 = gVu1.vi[kViMac]; uint32_t imac = mac0; unsigned mn = 0; int64_t ms[8]; uint32_t mv[8];   // MAC: the same',
            '    int bkReg = -1; uint32_t bkVal = 0; int bkLeft = 0;',
            '    auto BR = [&](int r) -> int16_t { r &= 15; if (bkLeft > 0 && bkReg == r) return (int16_t)bkVal; return r ? (int16_t)VIr1(r) : 0; };',
            '    (void)BR; (void)iclip; (void)cqs; (void)cqv; (void)cq0; (void)bkReg; (void)bkVal; (void)imac; (void)mac0; (void)ms; (void)mv;',
            'dispatch:',
            '    switch (pc) {']
    for pc in pcs:
        out.append(f'    case {pc}: goto L{pc:04x};')
    out += ['    default:',
            '        if (gLog) gLog("vu1: %s left its code at %04x: the interpreter goes on", "' + name + '", pc);',
            '        goto handover;',
            '    }']
    for pc in pcs:
        lo, hi, imm, ur, L, lr = info[pc]
        tag = 0xF0000000 | pc
        st = states.get(pc, {(-1, 0)})
        body = ['++cyc;']
        if hi & 0x40000000:
            body.append('ebit = 2;')
        if uses_bk:
            body.append('const int64_t before = cyc - 1;')
        reads = [(ur.r0, ur.f0), (ur.r1, ur.f1)]
        if not imm:
            reads += [(lr.r0, lr.f0), (lr.r1, lr.f1)]
        for r, fm in reads:
            if r and fm:
                for f in fields(fm):
                    if (r, f) in written:
                        body.append(f'if ({rv(r, f)} > cyc) cyc = {rv(r, f)};')
        if L is not None and L.branch:
            for r in ialu_regs:
                if L.bread & (1 << r):
                    body.append(f'if (ia{r} > cyc) cyc = ia{r};')
        if L is not None and L.fdiv:
            body.append('if (qB && qS + qC > cyc) cyc = qS + qC;')
        if L is not None and L.efu:
            body.append('if (gEfu.busy && gEfu.start + gEfu.cycles > cyc) cyc = gEfu.start + gEfu.cycles;')
        if uses_bk:
            body.append('if (bkLeft > 0) bkLeft -= (int)std::min<int64_t>(cyc - before, bkLeft);')
        w = hi & 0x01ffffff
        f1 = w & 0x3f
        f2 = ((w >> 4) & 0x7c) | (w & 3)
        reads_q = (f1 < 0x3c and f1 in (0x1c, 0x20, 0x21, 0x24, 0x25)) or (f1 >= 0x3c and f2 in (0x1c, 0x20, 0x21, 0x24, 0x25))
        if reads_q or (L is not None and L.fdiv):
            body.append('if (qB && cyc - qS >= qC) { gVu1.vi[kViQ] = qV; qB = false; }')
        if L is not None and (L.efu or L.lf2 == 0x64):
            body.append('if (gEfu.busy && cyc - gEfu.start >= gEfu.cycles) CompleteP();')
        if L is not None and L.lop in (0x10, 0x12, 0x13, 0x1c):       # the visible clip flags: the newest completed value
            body.append('{ uint32_t v = cq0; for (unsigned k = 1; k <= 8 && k <= cqn; ++k) { const unsigned i = (cqn - k) & 7; if (cyc - cqs[i] >= 4) { v = cqv[i]; break; } } gVu1.vi[kViClip] = v; }')
        vmac_read = L is not None and L.lop in (0x18, 0x1a, 0x1b)
        if vmac_read:                                                   # the visible MAC flags: the same
            body.append('uint32_t vmac = mac0; for (unsigned k = 1; k <= 8 && k <= mn; ++k) { const unsigned i = (mn - k) & 7; if (cyc - ms[i] >= 4) { vmac = mv[i]; break; } }')
        lower_reads_upper = (not imm) and ur.wr > 0 and (lr.r0 == ur.wr or lr.r1 == ur.wr)
        lower_dropped = (not imm) and ((ur.wr > 0 and lr.wr == ur.wr) or ((ur.viwrite & K_FLAG_CLIP) and L.lop == 0x11))
        if lower_reads_upper:
            body.append(f'uint32_t oldVf[4]; memcpy(oldVf, V.vf[{ur.wr}], 16);')
        up = upper_simd(hi, keep_mac) or upper_fast(hi, tag)
        if up:
            if not keep_mac:
                up = up.replace('ArithM(imac, ', 'ArithV1(')
                up = re.sub(r'MacR\(imac, \d, ', 'Clamp(', up)
            body.append(up)
        if imm:
            body.append(f'V.vi[kViI] = 0x{lo:08x}u;')
        elif not lower_dropped:
            if lower_reads_upper:
                body.append(f'uint32_t newVf[4]; memcpy(newVf, V.vf[{ur.wr}], 16); memcpy(V.vf[{ur.wr}], oldVf, 16);')
            code, bk = lower_fast(lo, L, pc, tag)
            if bk:
                body.append(f'const uint32_t viOld = VIr({bk[-1] & 15}) & 0xffff;')
            body += code
            if lower_reads_upper:
                body.append(f'memcpy(V.vf[{ur.wr}], newVf, 16);')
            if bk:
                r = bk[-1] & 15
                body.append(f'if (!(bkLeft > 0 && bkReg == {r})) {{ bkReg = {r}; bkVal = viOld; }}')
                body.append('bkLeft = 2;')
        if L is not None and L.ialu and (L.lft & 15) in ialu_regs:
            body.append(f'ia{L.lft & 15} = cyc + 4;')
        lf = lr.fmac and not imm
        if ur.fmac and ur.wr:
            for f in fields(ur.wf):
                body.append(f'{rv(ur.wr, f)} = cyc + 4;')
        if lf and lr.wr:
            for f in fields(lr.wf):
                body.append(f'{rv(lr.wr, f)} = cyc + 4;')
        flagreg = (ur.viwrite if ur.fmac else 0) | (lr.viwrite if lf else 0)
        if (ur.fmac or lf) and reads_mac:
            body.append('ms[mn & 7] = cyc; mv[mn & 7] = imac; ++mn;')
        if (ur.fmac or lf) and (flagreg & K_FLAG_CLIP):
            body.append('cqs[cqn & 7] = cyc; cqv[cqn & 7] = iclip; ++cqn;')
        # the next instruction: statically when no taken branch can be pending here, nor the E bit
        nxt = (pc + 8) & pcmask
        dynamic = (L is not None and L.branch) or any(a != -1 or e > 0 for (a, e) in st) or (hi & 0x40000000)
        if dynamic:
            body.append(f'uint32_t nextPc = {pc + 8}u;')
            body.append('if (armed >= 0) { nextPc = (uint32_t)armed; armed = -1; }')
            body.append('if (jump >= 0) { armed = jump; jump = -1; }')
            body.append(f'pc = nextPc & {pcmask}u;')
            body.append('if (ebit > 0 && ebit-- == 1) goto done;')
            body.append('if (++steps >= limit) goto done;')
            if nxt in pcset:
                body.append(f'if (pc == {nxt}u) goto L{nxt:04x};')
            body.append('goto dispatch;')
        else:
            body.append(f'pc = {nxt}u;')
            body.append('if (++steps >= limit) goto done;')
            body.append(f'goto L{nxt:04x};' if nxt in pcset else 'goto dispatch;')
        out.append(f'L{pc:04x}: {{                                   // {hi:08x} {lo:08x}')
        out += ['    ' + to_vu1(b) for b in body]
        out.append('}')
    out += ['done:',
            '    if (qB) gVu1.vi[kViQ] = qV;',
            '    CompleteP();',
            '    if (cqn) gVu1.vi[kViClip] = cqv[(cqn - 1) & 7];',
            ('    gVu1.vi[kViMac] = imac;' if keep_mac else '    // MAC: nothing reads what a program leaves (vu2cpp.py checked every MAC read)'),
            '    P.pc = pc; P.jump = jump; P.armed = armed; P.ebit = ebit; P.steps = steps;',
            '    return;',
            'handover:                                      // the interpreter goes on from here: its pipe state rebuilt',
            '    gMs.mac = imac;',
            '    { uint32_t v = mac0; for (unsigned k = 1; k <= 8 && k <= mn; ++k) { const unsigned i = (mn - k) & 7; if (cyc - ms[i] >= 4) { v = mv[i]; break; } } gVu1.vi[kViMac] = v; }',
            '    { uint32_t v = cq0; for (unsigned k = 1; k <= 8 && k <= cqn; ++k) { const unsigned i = (cqn - k) & 7; if (cyc - cqs[i] >= 4) { v = cqv[i]; break; } } gVu1.vi[kViClip] = v; }',
            '    gMs.cycle = cyc; gMs.clip = iclip; gMs.bkReg = bkReg; gMs.bkVal = bkVal; gMs.bkLeft = bkLeft;',
            '    if (qB) { gMs.qBusy = true; gMs.qStart = qS; gMs.qCycles = qC; gMs.qVal = qV; gMs.qStat = gMs.status; }',
            '    gMs.nslots = 0;']
    for r, f in sorted(written):
        out.append(f'    if ({rv(r, f)} > cyc && gMs.nslots < 8) {{ Slot& s = gMs.slot[gMs.nslots++]; s = Slot{{}}; s.start = {rv(r, f)} - 4; s.regU = {r}; s.fieldsU = {8 >> f}; s.mac = imac; s.status = gMs.status; s.clip = iclip; }}')
    out += ['    std::sort(gMs.slot, gMs.slot + gMs.nslots, [](const Slot& a, const Slot& b) { return a.start < b.start; });']
    for r in ialu_regs:
        out.append(f'    if (ia{r} > cyc && gMs.nialu < 4) gMs.ialu[gMs.nialu++] = {{ia{r} - 4, {1 << r}u}};')
    out += ['    P.pc = pc; P.jump = jump; P.armed = armed; P.ebit = ebit; P.steps = steps;',
            '    MicroLoop(P);',
            '}', '']
    return out


def lower_fast(lo, L, pc, tag, unit=1):
    """the lower instruction in the fast form: as lower_op, with the flag writes on the locals"""
    lop = L.lop
    pm, qm = (0x3ff8, 0x3ff) if unit else (0xff8, 0xff)
    if lop == 0x15:                                         # FSSET: the status flags are not kept
        return [], []
    if lop == 0x11:                                         # FCSET: the issue-side clip flags
        return [f'iclip = {lo & 0xffffff}u;'], []
    if lop == 0x40 and L.lf2 in (0x38, 0x39, 0x3a):         # DIV / SQRT / RSQRT: Q later (its value as Vu0Macro makes it)
        fs, ft = L.lfs, L.lft
        fsf, ftf = L.fsf, L.ftf
        if L.lf2 == 0x38:
            val = (f'{{ const uint32_t ub = V.vf[{ft}][{ftf}], ua = V.vf[{fs}][{fsf}]; const float b = Op(ub), a = Op(ua);'
                   f' qV = b == 0.0f ? (((ub ^ ua) & 0x80000000) ? 0xff7fffffu : 0x7f7fffffu) : U(Op(U(a / b))); }}')
        elif L.lf2 == 0x39:
            val = f'{{ const float b = Op(V.vf[{ft}][{ftf}]); qV = U(Op(U(sqrtf(fabsf(b))))); }}'
        else:
            val = (f'{{ const uint32_t ub = V.vf[{ft}][{ftf}], ua = V.vf[{fs}][{fsf}]; const float b = Op(ub), a = Op(ua);'
                   f' if (b == 0.0f) qV = a != 0.0f ? (((ub ^ ua) & 0x80000000) ? 0xff7fffffu : 0x7f7fffffu) : (((ub ^ ua) & 0x80000000) ? 0x80000000u : 0u);'
                   f' else qV = U(Op(U(a / sqrtf(fabsf(b))))); }}')
        return [val, f'qB = true; qS = cyc; qC = {13 if L.lf2 == 0x3a else 7};'], []
    if lop == 0x40 and L.lf2 >= 0x64 and L.lf2 != 0x64 and L.lf2 not in (0x68, 0x69, 0x6c):
        c, bk = lower_op(lo, L, pc, pm, qm, unit, tag)
        return ['gMs.cycle = cyc;'] + c, bk                # EFU: its start is the current cycle
    c, bk = lower_op(lo, L, pc, pm, qm, unit, tag)
    if lop in (0x18, 0x1a, 0x1b):
        c = [x.replace('V.vi[kViMac]', 'vmac') for x in c]
    return c, bk


# ---- the static form: a VU1 program whose pipeline timing is worked out here. Each instruction is written
# once per pipeline state it can be reached in (the VF fields still being written and in how many cycles,
# the pending Q and P, the integer loads branches wait for, the MAC and clip results not visible yet, the
# branches' integer backup): its stalls, and which value each Q, MAC, clip, P or backup read sees, are the
# fast form's, decided here, so the code keeps no cycle count. A program reaching too many states keeps the
# fast form.
STATIC_MAX_STATES = 40          # states of one instruction
STATIC_MAX_NODES = 12000        # instructions written, all states
STATIC_NO_CLEAN = False         # --no-clean: every operand through Op()


def gen_program_static(name, micro, pcs, entries, states, keep_mac, unit=1, status=False):
    pcmask = 0x3ff8 if unit else 0xff8
    pcset = set(pcs)
    info = {}
    bread_all = 0
    loaded = set()
    reads_mac = reads_clip = False
    links = set()
    for pc in pcs:
        lo, hi = struct.unpack_from('<II', micro, pc)
        imm = (hi & 0x80000000) != 0
        ur = upper_regs(hi)
        L = None if imm else lower_info(lo)
        lr = Regs() if imm else L.r
        info[pc] = (lo, hi, imm, ur, L, lr)
        if L is not None:
            if L.ialu:
                loaded.add(L.lft & 15)
            if L.branch:
                bread_all |= L.bread
            if L.lop in (0x18, 0x1a, 0x1b):
                reads_mac = True
            if L.lop in (0x10, 0x12, 0x13, 0x1c):
                reads_clip = True
    if not unit:
        # VU0's: the EE reads its flags after the run (CFC2), so the exit works out MAC, clip and status as the
        # interpreter's pipeline leaves them; a program that reads or sets the status inside, uses Q, a register
        # jump or a link keeps the full form (status: the form below that keeps the status, --vu0-static)
        for (lo, hi, imm, ur, L, lr) in info.values():
            if L is not None and (L.lop == 0x40 and L.lf2 >= 0x64):
                return None
            if not status and L is not None and (L.lop in (0x14, 0x15, 0x16, 0x17, 0x21, 0x24, 0x25)
                                                 or (L.lop == 0x40 and L.lf2 in (0x38, 0x39, 0x3a, 0x3b))):
                return None
        keep_mac = True
    # (v141-host beta 13, --vu0-static) a VU0 program that reads or sets its status keeps it instruction by instruction:
    # the issue side (ist: each MAC writer's ZSUO and their sticky copies, FSSET, the divider's I and D), each FMAC pair's
    # slot visible 4 cycles later (vst, MicroRun's CompleteSlot), the divider's bits when Q completes
    reads_status = status and not unit
    keep_left = keep_mac                                # the MAC a program leaves is read (by another one)
    keep_mac = keep_mac or reads_mac
    uses_bk = any(L is not None and L.lop in (0x28, 0x29, 0x2c, 0x2d, 0x2e, 0x2f) for (_, _, _, _, L, _) in info.values())
    ialu_regs = set(r for r in loaded if bread_all & (1 << r))
    # return addresses: every BAL / JALR's link (a JR may reach any of them)
    for pc in pcs:
        lo, hi, imm, ur, L, lr = info[pc]
        if L is not None and L.lop in (0x21, 0x25):
            for (a, e) in states.get(pc, {(-1, 0)}):
                if a == DYN:
                    return None
                links.add((((a if a >= 0 else pc + 8) + 8)) & pcmask)

    def mac_writer(hi):
        w = hi & 0x01ffffff
        f1 = w & 0x3f
        f2 = ((w >> 4) & 0x7c) | (w & 3)
        return upper_writes_flags(hi) and not (f1 >= 0x3c and f2 == 0x1f)

    def clip_writer(hi, L, imm):
        w = hi & 0x01ffffff
        f2 = ((w >> 4) & 0x7c) | (w & 3)
        return ((w & 0x3f) >= 0x3c and f2 == 0x1f) or (not imm and L.lop == 0x11)

    def bk_regs(lo, L, imm):
        if imm or L is None:
            return []
        if L.lop in (0x08, 0x09):
            return [L.lft] if L.lft & 15 else []
        if L.lop == 0x40 and L.lf2 < 0x64 and L.lf2 not in (0x38, 0x39, 0x3a, 0x3b):
            lf1, lf2 = L.lf1, L.lf2
            if lf1 in (0x30, 0x31, 0x34, 0x35):
                return [(lo >> 6) & 15] if (lo >> 6) & 15 else []
            if lf1 == 0x32 or lf2 == 0x3c:
                return [L.lft] if L.lft & 15 else []
            if lf2 in (0x34, 0x36):
                return [L.lfs]
            if lf2 in (0x35, 0x37):
                return [L.lft]
        return []

    # a pipe state: (vf, ia, q, p, mac, clip, bk, st) with vf ((reg, field, cycles left), ...), ia ((reg, left), ...),
    # q / p cycles left (0: none), mac / clip ((left, variable), ...) oldest first, bk (reg, left) or None, st the status
    # slots ((left, variable, FSSET's), ...) oldest first (reads_status only)
    EMPTY = ((), (), 0, 0, (), (), None, ())

    def step(pc, armed, ebit, pipe):
        """what one instruction does in a state: its stall, the completions before it, the state after it"""
        lo, hi, imm, ur, L, lr = info[pc]
        vf, ia, q, p, mac, clip, bk, sts = pipe
        delta = 1
        reads = [(ur.r0, ur.f0), (ur.r1, ur.f1)]
        if not imm:
            reads += [(lr.r0, lr.f0), (lr.r1, lr.f1)]
        for r, fm in reads:
            if r and fm:
                for (vr, f, left) in vf:
                    if vr == r and fm & (8 >> f):
                        delta = max(delta, left)
        if L is not None and L.branch:
            for (r, left) in ia:
                if L.bread & (1 << r):
                    delta = max(delta, left)
        if L is not None and L.fdiv and q:
            delta = max(delta, q)
        if L is not None and L.efu and p:
            delta = max(delta, p)
        ev = {'q': False, 'p': False, 'mac': [], 'clip': [], 'st': []}
        vf = tuple(x for x in ((r, f, left - delta) for (r, f, left) in vf) if x[2] > 0)
        ia = tuple(x for x in ((r, left - delta) for (r, left) in ia) if x[1] > 0)
        if q:
            q -= delta
            if q <= 0:
                q = 0; ev['q'] = True
        if p:
            p -= delta
            if p <= 0:
                p = 0; ev['p'] = True
        nm = []
        for (left, var) in mac:
            if left - delta <= 0:
                ev['mac'].append(var)
            else:
                nm.append((left - delta, var))
        nc = []
        for (left, var) in clip:
            if left - delta <= 0:
                ev['clip'].append(var)
            else:
                nc.append((left - delta, var))
        ns = []
        for (left, var, fsp) in sts:
            if left - delta <= 0:
                ev['st'].append((var, fsp))
            else:
                ns.append((left - delta, var, fsp))
        if bk is not None:
            r, left = bk
            left -= min(delta, left)
            bk = (r, left) if left > 0 else None
        ev['bk'] = bk                                   # the backup branches of this instruction see
        # this instruction's effects
        lower_dropped = (not imm) and ((ur.wr > 0 and lr.wr == ur.wr) or ((ur.viwrite & K_FLAG_CLIP) and L.lop == 0x11))
        lf = lr.fmac and not imm
        writes = set()
        if ur.fmac and ur.wr:
            writes |= {(ur.wr, f) for f in fields(ur.wf)}
        if lf and lr.wr:
            writes |= {(lr.wr, f) for f in fields(lr.wf)}
        vf = tuple(sorted([x for x in vf if (x[0], x[1]) not in writes] + [(r, f, 4) for (r, f) in writes]))
        if L is not None and L.ialu and (L.lft & 15) in ialu_regs:
            ia = tuple(sorted([x for x in ia if x[0] != (L.lft & 15)] + [((L.lft & 15), 4)]))
        if L is not None and not lower_dropped and L.lop == 0x40 and L.lf2 in (0x38, 0x39, 0x3a):
            q = 13 if L.lf2 == 0x3a else 7
        efu_lat = {0x70: 11, 0x71: 18, 0x72: 18, 0x73: 24, 0x74: 54, 0x75: 54, 0x76: 12, 0x78: 12, 0x79: 18, 0x7a: 12, 0x7c: 29, 0x7d: 54, 0x7e: 44}
        if L is not None and not lower_dropped and L.lop == 0x40 and L.lf2 in efu_lat:
            p = efu_lat[L.lf2]
        ev['macvar'] = ev['clipvar'] = ev['stvar'] = None
        if reads_status and (ur.fmac or lf):
            used = {v for (_, v, _) in ns}
            var = min(v for v in range(8) if v not in used)
            fsp = (not imm) and L.lop == 0x15 and not lower_dropped
            ns.append((4, var, fsp)); ev['stvar'] = (var, fsp)
        if reads_mac and (ur.fmac or lf) and mac_writer(hi):
            used = {v for (_, v) in nm}
            var = min(v for v in range(8) if v not in used)
            nm.append((4, var)); ev['macvar'] = var
        if reads_clip and (ur.fmac or lf) and clip_writer(hi, L, imm):
            used = {v for (_, v) in nc}
            var = min(v for v in range(8) if v not in used)
            nc.append((4, var)); ev['clipvar'] = var
        ev['bkchain'] = False
        if not lower_dropped and uses_bk:
            bks = bk_regs(lo, L, imm)
            if bks:
                r = bks[-1] & 15
                ev['bkchain'] = bk is not None and bk[0] == r
                bk = (r, 2)
        pipe2 = (vf, ia, q, p, tuple(nm), tuple(nc), bk, tuple(ns))
        # the next instruction
        e = 2 if hi & 0x40000000 else ebit
        nxt = []                                        # (condition, pc or 'jt', armed after, ebit after)
        jump = None
        if L is not None and L.branch:
            target = (pc + 8 + imm11(lo) * 8) & pcmask
            if L.lop in (0x20, 0x21):
                jump = [('', target)]
            elif L.lop in (0x24, 0x25):
                jump = [('', DYN)]
            else:
                jump = [('t', target), ('f', None)]
        else:
            jump = [('', None)]
        ends = False
        if e > 0:
            ends = e == 1
            e -= 1
        for cond, j in jump:
            nextpc = pc + 8
            if armed >= 0:
                nextpc = armed
            elif armed == DYN:
                nextpc = 'jt'
            a2 = -1 if j is None else j
            nxt.append((cond, nextpc if nextpc == 'jt' else nextpc & pcmask, a2, e))
        return delta, ev, pipe2, nxt, ends

    # the states each instruction is reached in
    nodes = {}
    order = []
    work = []
    def node(pc, armed, ebit, pipe):
        k = (pc, armed, ebit, pipe)
        if k not in nodes:
            nodes[k] = len(order)
            order.append(k)
            work.append(k)
        return nodes[k]
    starts = {e: node(e & pcmask, -1, 0, EMPTY) for e in entries}
    succ = {}
    per_pc = {}
    while work:
        k = work.pop()
        pc, armed, ebit, pipe = k
        if pc not in pcset:
            return None
        if armed == DYN and info[pc][4] is not None and info[pc][4].lop in (0x24, 0x25):
            return None                                 # a register jump in a register jump's delay slot
        per_pc[pc] = per_pc.get(pc, 0) + 1
        if per_pc[pc] > STATIC_MAX_STATES or len(order) > STATIC_MAX_NODES:
            return None
        delta, ev, pipe2, nxt, ends = step(pc, armed, ebit, pipe)
        out = []
        for cond, npc, a2, e2 in nxt:
            if ends:
                out.append((cond, ('done', npc, a2, e2)))
            elif npc == 'jt':
                out.append((cond, ('jt', [(l, node(l, a2, e2, pipe2)) for l in sorted(links)], a2, e2, pipe2)))
            else:
                out.append((cond, ('n', node(npc, a2, e2, pipe2))))
        succ[k] = (delta, ev, pipe2, out, ends)

    # the MAC results something reads: a MAC read (FMAND, FMEQ, FMOR) sees the value of a slot that matured,
    # which an arithmetic instruction made (or OPMULA / OPMSUB, which keep the w lane's bits of the one before).
    # Only those instructions work out their MAC flags.
    needed = set()
    if reads_mac:
        def partial(hi):
            w = hi & 0x01ffffff
            f1 = w & 0x3f
            f2 = ((w >> 4) & 0x7c) | (w & 3)
            return (f1 < 0x3c and f1 == 0x2e) or (f1 >= 0x3c and f2 == 0x2e)
        ENTRY = 1 << len(order)                         # the MAC the program starts with
        nin = {0: None}
        state_in = {i: None for i in range(len(order))}
        for e in entries:
            state_in[starts[e]] = (ENTRY, (0,) * 8, ENTRY)
        deps = {}                                       # a partial writer: the definitions of the MAC before it
        todo = [starts[e] for e in entries]
        while todo:
            i = todo.pop()
            imacd, mqd, vmacd = state_in[i]
            mqd = list(mqd)
            k = order[i]
            delta, ev, pipe2, nxt, ends = succ[k]
            lo, hi, imm, ur, L, lr = info[k[0]]
            for var in ev['mac']:
                vmacd = mqd[var]
            if L is not None and L.lop in (0x18, 0x1a, 0x1b):
                m = vmacd
                while m:
                    b = m & -m; m ^= b
                    if b != ENTRY:
                        needed.add(b.bit_length() - 1)
            if ev['macvar'] is not None:
                if partial(hi):
                    deps[i] = deps.get(i, 0) | imacd
                    imacd = imacd | (1 << i)
                else:
                    imacd = 1 << i
                mqd[ev['macvar']] = imacd
            out_state = (imacd, tuple(mqd), vmacd)
            for cnd, t in succ[k][3]:
                targets = [t[1]] if t[0] == 'n' else [n for (_, n) in t[1]] if t[0] == 'jt' else []
                for n in targets:
                    old = state_in[n]
                    if old is None:
                        state_in[n] = out_state; todo.append(n)
                    else:
                        merged = (old[0] | out_state[0], tuple(a | b for a, b in zip(old[1], out_state[1])), old[2] | out_state[2])
                        if merged != old:
                            state_in[n] = merged; todo.append(n)
        if keep_left:
            needed = set(range(len(order)))
        grow = list(needed)                             # a needed partial writer needs the MAC before it
        while grow:
            i = grow.pop()
            m = deps.get(i, 0)
            while m:
                b = m & -m; m ^= b
                j = b.bit_length() - 1
                if b != ENTRY and j not in needed:
                    needed.add(j); grow.append(j)

    if keep_left:                                       # the MAC the run leaves is read: every writer works it out
        needed = set(range(len(order)))

    # the operands known clean at each instruction (Op() would leave them as they are: no clamp needed)
    clean_in = {}
    todo = []
    for e in entries:
        clean_in[starts[e]] = 0xf                       # VF0 (0, 0, 0, 1)
        todo.append(starts[e])
    while todo:
        i = todo.pop()
        k = order[i]
        delta, ev, pipe2, nxt, ends = succ[k]
        lo, hi, imm, ur, L, lr = info[k[0]]
        cin = clean_in[i]
        if ev['q']:
            cin |= 1 << 133
        lower_dropped = (not imm) and ((ur.wr > 0 and lr.wr == ur.wr) or ((ur.viwrite & K_FLAG_CLIP) and L.lop == 0x11))
        cout = clean_after(hi, lo, imm, L, lower_dropped, cin)
        for cnd, t in succ[k][3]:
            targets = [t[1]] if t[0] == 'n' else [n for (_, n) in t[1]] if t[0] == 'jt' else []
            for n in targets:
                old = clean_in.get(n)
                new = cout if old is None else (old & cout)
                if new != old:
                    clean_in[n] = new; todo.append(n)

    # the code
    out = [f'// {name}: VU{unit} (static form{"" if keep_mac else ", no MAC"}{", status kept" if reads_status else ""}), entries {" ".join("%04x" % e for e in entries)}, {len(pcs)} instructions, {len(order)} states',
           f'void {name}(MicroPos& P) {{',
           '    Ctx dummy{}; (void)dummy;',
           '    long steps = P.steps;',
           '    const long limit = gVuStepLimit;',
           '    uint32_t qv = 0, jt = 0, bkv = 0;',
           '    uint32_t iclip = gVu1.vi[kViClip];',
           '    uint32_t imac = gVu1.vi[kViMac], vmac = imac;',
           '    uint32_t mq[8] = {}, cq[8] = {};',
           '    (void)qv; (void)jt; (void)bkv; (void)iclip; (void)imac; (void)vmac; (void)mq; (void)cq;']
    if reads_status:                                    # the status: issue side, visible, Q's, the slots'
        out += ['    uint32_t ist = gVu1.vi[kViStatus], vst = ist, qst = 0, sq[8] = {};',
                '    (void)qst; (void)sq;']
    elif not unit:                                      # the status the run starts with, the MAC of its updates, whether
        out += ['    const uint32_t st0 = gVu1.vi[kViStatus];',  # one ran, whether a slot took the status before it
                '    uint32_t macor = 0; bool fl = false, pre = false;',
                '    (void)st0; (void)macor; (void)fl; (void)pre;']
    out.append('    switch (P.pc) {')
    for e in sorted(entries):
        out.append(f'    case {e & pcmask}: goto N{starts[e]};')
    out += ['    default: Vu1HandOver(P, iclip, imac); return;' if unit else '    default: Vu0HandOver(P); return;',
            '    }']

    def st_done(var, fsp):
        """a status slot completes (MicroRun's CompleteSlot): ZSUO and their sticky copies, or FSSET's sticky bits"""
        if fsp:
            return f'vst = (vst & 0x30u) | (sq[{var}] & 0xfc0u) | (sq[{var}] & 0xfu);'
        return f'vst = (vst & 0xff0u) | (sq[{var}] & 0xfu) | ((sq[{var}] & 0xfu) << 6);'

    def st_flush(pipe):
        """the status at the run's end (MicroRun's FlushAll: Q first, then the slots in order)"""
        c = ['vst = (vst & 0xfcfu) | (qst & 0xc30u);'] if pipe[2] else []
        c += [st_done(var, fsp) for (_, var, fsp) in pipe[7]]
        return c + ['gVu1.vi[kViStatus] = vst;']

    def exit_code(pipe, pc_expr, armed, ebit):
        """the end of the run in a state: what is pending completed, the place for MSCNT"""
        c = []
        if reads_status:                                # VU0 with its status kept: Q, the slots, MAC and clip
            assert not pipe[3]
            if pipe[2]:
                c.append('gVu1.vi[kViQ] = qv;')
            c += st_flush(pipe)
            c += ['gVu1.vi[kViClip] = iclip;', 'gVu1.vi[kViMac] = imac;']
            c.append(f'P.pc = {pc_expr}; P.jump = -1; P.armed = {armed if armed != DYN else "(int64_t)jt"}; P.ebit = {ebit}; P.steps = steps;')
            return c + ['return;']
        if not unit:                                    # VU0: no Q nor P here; MAC, clip and status as the pipeline's end
            assert not pipe[2] and not pipe[3]
            c += ['gVu1.vi[kViClip] = iclip;', 'gVu1.vi[kViMac] = imac;',
                  'gVu1.vi[kViStatus] = (st0 & 0xff0u) | (MacNib(macor) << 6) | (pre ? (st0 & 0xfu) << 6 : 0u) | (fl ? MacNib(imac) : (st0 & 0xfu));',
                  f'P.pc = {pc_expr}; P.jump = -1; P.armed = {armed}; P.ebit = {ebit}; P.steps = steps;', 'return;']
            return c
        if pipe[2]:
            c.append('gVu1.vi[kViQ] = qv;')
        c.append('CompleteP();')
        c.append('gVu1.vi[kViClip] = iclip;')
        if keep_left:
            c.append('gVu1.vi[kViMac] = imac;')
        c.append(f'P.pc = {pc_expr}; P.jump = -1; P.armed = {armed if armed != DYN else "(int64_t)jt"}; P.ebit = {ebit}; P.steps = steps;')
        c.append('return;')
        return c

    def handover(pipe, pc_expr, armed, ebit):
        if reads_status:                                # VU0 (under the micro lock): the interpreter goes on with the pipe
            c = ['if (gLog) gLog("vu0: %s left its code at %04x: the interpreter goes on", "' + name + '", (unsigned)(' + pc_expr + '));']
            if pipe[2]:
                c.append('gVu1.vi[kViQ] = qv;')
            c += st_flush(pipe) + ['gMs.status = ist;', 'gVu1.vi[kViClip] = iclip;',
                                   'gMs.clip = iclip; gMs.mac = imac; gVu1.vi[kViMac] = imac; gMs.nslots = 0; gMs.nialu = 0; gMs.qBusy = false; gMs.bkLeft = 0;']
            c.append(f'P.pc = {pc_expr}; P.jump = -1; P.armed = {armed if armed != DYN else "(int64_t)jt"}; P.ebit = {ebit}; P.steps = steps;')
            return c + ['MicroLoop(P);', 'return;']
        assert unit                                     # (a VU0 static program without its status has no register jump)
        c = ['if (gLog) gLog("vu1: %s left its code at %04x: the interpreter goes on", "' + name + '", (unsigned)(' + pc_expr + '));']
        if pipe[2]:
            c.append('gVu1.vi[kViQ] = qv;')
        c += ['CompleteP();', 'gVu1.vi[kViClip] = iclip;', 'gVu1.vi[kViMac] = imac;']
        c.append(f'P.pc = {pc_expr}; P.jump = -1; P.armed = {armed if armed != DYN else "(int64_t)jt"}; P.ebit = {ebit}; P.steps = steps;')
        c += ['Vu1HandOver(P, iclip, imac);', 'return;']    # the interpreter, under the micro lock (vu0.cpp)
        return c

    conv = to_vu1s if unit else to_vu0s
    exits = set()
    for idx, k in enumerate(order):
        pc, armed, ebit, pipe = k
        delta, ev, pipe2, nxt, ends = succ[k]
        lo, hi, imm, ur, L, lr = info[pc]
        tag = 0xF0000000 | pc
        body = []
        if ev['q']:
            body.append('gVu1.vi[kViQ] = qv;')
        if ev['p']:
            body.append('CompleteP();')
        for var in ev['mac']:
            body.append(f'vmac = mq[{var}];')
        for var in ev['clip']:
            body.append(f'gVu1.vi[kViClip] = cq[{var}];')
        if reads_status:                                # (the slots complete, then Q: MicroRun's TestPipes)
            body += [st_done(var, fsp) for (var, fsp) in ev['st']]
            if ev['q']:
                body.append('vst = (vst & 0xfcfu) | (qst & 0xc30u);')
        lower_reads_upper = (not imm) and ur.wr > 0 and (lr.r0 == ur.wr or lr.r1 == ur.wr)
        lower_dropped = (not imm) and ((ur.wr > 0 and lr.wr == ur.wr) or ((ur.viwrite & K_FLAG_CLIP) and L.lop == 0x11))
        if lower_reads_upper:
            body.append(f'uint32_t oldVf[4]; memcpy(oldVf, V.vf[{ur.wr}], 16);')
        mac_here = keep_mac and idx in needed
        cin = clean_in.get(idx, 0) | ((1 << 133) if ev['q'] else 0)
        def isclean(r, l, cin=cin):
            bit = 132 if r == 'i' else 133 if r == 'q' else 128 + l if r == 'acc' else 4 * r + l
            return (cin >> bit) & 1 == 1
        up = (upper_simd_clean(hi, mac_here, isclean) if not STATIC_NO_CLEAN else upper_simd(hi, mac_here)) or upper_fast(hi, tag)
        if up:
            if not mac_here:
                up = up.replace('ArithM(imac, ', 'ArithV1(')
                up = re.sub(r'MacR\(imac, \d, ', 'Clamp(', up)
            body.append(up)
        if reads_status:                                # the issue side's ZSUO and their sticky copies (StatusUpdate)
            if mac_writer(hi):
                body.append('ist = (ist & 0xff0u) | MacNib(imac) | (MacNib(imac) << 6);')
        elif not unit:                                  # VU0's status: each MAC writer updates it (StatusUpdate); a slot
            if mac_writer(hi):                          # taken before the first gets the status the run started with
                body.append('macor |= imac; fl = true;')
            elif ur.fmac or (lr.fmac and not imm):
                body.append('pre |= !fl;')
        cond_expr = None
        if imm:
            body.append(f'V.vi[kViI] = 0x{lo:08x}u;')
        elif not lower_dropped:
            if lower_reads_upper:
                body.append(f'uint32_t newVf[4]; memcpy(newVf, V.vf[{ur.wr}], 16); memcpy(V.vf[{ur.wr}], oldVf, 16);')
            bks = bk_regs(lo, L, imm) if uses_bk else []
            if bks:
                body.append(f'const uint32_t viOld = VIr({bks[-1] & 15}) & 0xffff;')
            bk = ev['bk']
            def BRs(r):
                r &= 15
                if bk is not None and bk[0] == r:
                    return '(int16_t)bkv'
                return f'(int16_t)VIr({r})' if r else '0'
            lop = L.lop
            if L.branch:
                lft, lfs = L.lft, L.lfs
                if lop in (0x28, 0x29, 0x2c, 0x2d, 0x2e, 0x2f):
                    cond_expr = {0x28: f'{BRs(lft)} == {BRs(lfs)}', 0x29: f'{BRs(lft)} != {BRs(lfs)}', 0x2c: f'{BRs(lfs)} < 0',
                                 0x2d: f'{BRs(lfs)} > 0', 0x2e: f'{BRs(lfs)} <= 0', 0x2f: f'{BRs(lfs)} >= 0'}[lop]
                elif lop in (0x21, 0x25):
                    link = ((((armed if armed >= 0 else pc + 8) + 8) >> 3) & 0xffff)
                    if lop == 0x25:
                        body.append(f'{{ const uint32_t t = ((VIr({lfs}) & 0xffff) * 8) & {pcmask}u;')
                        if lft & 15:
                            body.append(f'VIr({lft}) = {link}u;')
                        body.append('jt = t; }')
                    else:
                        if lft & 15:
                            body.append(f'VIr({lft}) = {link}u;')
                elif lop == 0x24:
                    body.append(f'jt = ((VIr({lfs}) & 0xffff) * 8) & {pcmask}u;')
            else:
                code, _ = lower_fast(lo, L, pc, tag, unit)
                if lop == 0x40 and L.lf2 in (0x38, 0x39, 0x3a):
                    code = [code[0].replace('qV = ', 'qv = ')]
                    if reads_status:                    # the divider's I and D on the issue side, visible when Q completes
                        fs_, ft_, fsf_, ftf_ = L.lfs, L.lft, L.fsf, L.ftf
                        if L.lf2 == 0x38:
                            code.append(f'{{ const float b = Op(V.vf[{ft_}][{ftf_}]), a = Op(V.vf[{fs_}][{fsf_}]); ist &= ~0x30u;'
                                        f' if (b == 0.0f) ist |= (a == 0.0f) ? 0x410u : 0x820u; qst = ist; }}')
                        elif L.lf2 == 0x39:
                            code.append(f'{{ const float b = Op(V.vf[{ft_}][{ftf_}]); ist &= ~0x30u; if (b < 0.0f) ist |= 0x410u; qst = ist; }}')
                        else:
                            code.append(f'{{ const float b = Op(V.vf[{ft_}][{ftf_}]), a = Op(V.vf[{fs_}][{fsf_}]); ist &= ~0x30u;'
                                        f' if (b == 0.0f) {{ ist |= 0x820u; if (a == 0.0f) ist |= 0x410u; }} else if (b < 0.0f) ist |= 0x410u; qst = ist; }}')
                elif code and code[0] == 'gMs.cycle = cyc;':
                    code = code[1:]
                if reads_status and lop == 0x15:        # FSSET: the issue side's sticky bits
                    i12 = ((lo >> 10) & 0x800) | (lo & 0x7ff)
                    code = [f'ist = {i12 & 0xfc0}u | (ist & 0x3fu);']
                elif reads_status and lop in (0x14, 0x16, 0x17):   # FSEQ FSAND FSOR read the visible status
                    code = [x.replace('V.vi[kViStatus]', 'vst') for x in code]
                body += code
            if lower_reads_upper:
                body.append(f'memcpy(V.vf[{ur.wr}], newVf, 16);')
            if bks and not ev['bkchain']:
                body.append('bkv = viOld;')
        if ev['macvar'] is not None and mac_here:
            body.append(f'mq[{ev["macvar"]}] = imac;')
        if ev['clipvar'] is not None:
            body.append(f'cq[{ev["clipvar"]}] = iclip;')
        if ev['stvar'] is not None:
            body.append(f'sq[{ev["stvar"][0]}] = ist;')
        if not ends:
            body.append('++steps;')                     # (the fast form counts no instruction that ends the run)
        # where it goes
        def go(target):
            kind = target[0]
            if kind == 'n':
                if target[1] > idx:                     # forward in the code's order: no loop closes there, no limit check
                    return [f'goto N{target[1]};']
                exits.add((idx, target[1]))
                return [f'if (steps >= limit) goto X{idx}_{target[1]}; goto N{target[1]};']
            if kind == 'done':
                _, npc, a2, e2 = target
                if npc == 'jt':
                    return exit_code(pipe2, 'jt', a2, e2)
                return exit_code(pipe2, f'{npc}u', a2, e2)
            _, cases, a2, e2, p2 = target
            c = ['if (steps >= limit) { ' + ' '.join(exit_code(p2, 'jt', a2, e2)) + ' }', 'switch (jt) {']
            c += [f'case {l}u: goto N{n};' for (l, n) in cases]
            c += ['default: { ' + ' '.join(handover(p2, 'jt', a2, e2)) + ' }', '}']
            return c
        outs = succ[k][3]
        if len(outs) == 1:
            body += go(outs[0][1])
        else:
            t = [o for (cnd, o) in outs if cnd == 't'][0]
            f = [o for (cnd, o) in outs if cnd == 'f'][0]
            body.append(f'if ({cond_expr}) {{')
            body += ['    ' + b for b in go(t)]
            body.append('}')
            body += go(f)
        st = 'vf ' + ','.join(f'{r}{"xyzw"[f]}{l}' for (r, f, l) in pipe[0]) if pipe[0] else ''
        out.append(f'N{idx}: {{                                   // {pc:04x} {hi:08x} {lo:08x} {st}{" q" + str(pipe[2]) if pipe[2] else ""}{" bk" if pipe[6] else ""}')
        out += ['    ' + conv(b) for b in body]
        out.append('}')
    # the limit's exits (the step budget spent between two instructions): the next instruction's place
    for (idx, n) in sorted(exits):
        npc, a2, e2, p2 = order[n]
        out.append(f'X{idx}_{n}: {{ ' + ' '.join(conv(x) for x in exit_code(p2, f'{npc}u', a2, e2)) + ' }')
    out += ['}', '']
    if not unit:                                        # (the header's names; the code's went through to_vu0s)
        out = [x.replace('gVu1', 'gVu0') for x in out]
    if not any('dummy' in x for x in out[3:]):
        out[2] = '    // (no instruction of it goes through Vu0Macro)'
    return out


def full_arith(hi):
    """an upper instruction that sets every MAC field (Arith: its lanes set, the others cleared)"""
    w = hi & 0x01ffffff
    f1 = w & 0x3f
    if f1 < 0x3c:
        return f1 <= 0x0f or 0x18 <= f1 <= 0x1c or f1 in (0x1e, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2c, 0x2d)
    f2 = ((w >> 4) & 0x7c) | (w & 3)
    return f2 <= 0x0f or 0x18 <= f2 <= 0x1c or f2 in (0x1e, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2c, 0x2d)


def mac_reads_own(micro, pcs, entries, edges):
    """whether every MAC read (FMEQ / FMAND / FMOR) of the program sees a MAC its own
    arithmetic made: on every path an Arith at least 4 instructions before the read (the
    read then sees a completed slot issued after it). Then the MAC a program starts with is
    never read."""
    NONE = 0
    val = {pc: None for pc in pcs}
    preds = {}
    for a, b in edges:
        preds.setdefault(b, set()).add(a)
    arith = {pc: full_arith(struct.unpack_from('<I', micro, pc + 4)[0]) and not struct.unpack_from('<I', micro, pc + 4)[0] & 0 for pc in pcs}
    def out_of(pc, vin):
        if vin == NONE:
            return 1 if arith[pc] else NONE
        return min(vin + 1, 4)
    changed = True
    vin = {pc: 4 for pc in pcs}                         # the lattice top; entries start at NONE
    for e in entries:
        vin[e] = NONE
    while changed:
        changed = False
        for pc in pcs:
            v = NONE if pc in entries else 4
            ps = preds.get(pc, set())
            if pc not in entries and not ps:
                v = 4
            for q in ps:
                if q in vin:
                    v = min(v, out_of(q, vin[q]))
            if pc in entries:
                v = NONE
            if v != vin[pc]:
                vin[pc] = v
                changed = True
    for pc in pcs:
        lo, hi = struct.unpack_from('<II', micro, pc)
        if not hi & 0x80000000 and (lo >> 25) in (0x18, 0x1a, 0x1b) and vin[pc] < 4:
            return False
    return True


def page_images(folder):
    """the images of tools/ps2_images.py by name: 'base' (mgs3_n_base.bin), '<page>' (<page>_<file>.bin)"""
    out = {}
    for f in sorted(glob.glob(os.path.join(folder, '*.bin'))):
        n = os.path.basename(f)[:-4]
        out['base' if n == 'mgs3_n_base' else n.rsplit('_', 1)[0]] = open(f, 'rb').read()
    if 'base' not in out:
        sys.exit(f'no mgs3_n_base.bin in {folder}')
    return out


def write_manifest(images, folder, path):
    """each program start's reachable code as pieces of the images (the longest match first)"""
    srcs = page_images(folder)
    order = ['base'] + sorted(k for k in srcs if k != 'base')
    lines = ['# the VU microprograms of MGO1: where the code each program start runs is in the images',
             '# tools/ps2_images.py writes (offsets only). recomp/vu2cpp.py --manifest reads it.',
             '# unit entry check(FNV-1a 64 of the pieces) pieces(micro address:image:offset:length)']
    for (unit, entry, _), micro in sorted(images.items()):
        pcs, _ = reachable(micro, entry, 0x3ff8 if unit else 0xff8)
        pieces, chk = [], 1469598103934665603
        for lo, hi in ranges_of(pcs):
            pos = lo
            while pos < hi:
                best = None
                for name in order:
                    s = srcs[name]
                    i = s.find(micro[pos:pos + 8])
                    while i >= 0:
                        k = 8
                        while pos + k < hi and i + k < len(s) and s[i + k] == micro[pos + k]:
                            k += 1
                        if not best or k > best[2]:
                            best = (name, i, k)
                        if k == hi - pos:
                            break
                        i = s.find(micro[pos:pos + 8], i + 1)
                    if best and best[2] == hi - pos:
                        break
                if not best:
                    sys.exit(f'vu{unit} entry {entry:#x}: code at {pos:#x} is in none of the images')
                pieces.append(f'{pos:x}:{best[0]}:{best[1]:x}:{best[2]:x}')
                chk = fnv(micro[pos:pos + best[2]], chk)
                pos += best[2]
        line = f'{unit} {entry:x} {chk:016x} ' + ' '.join(pieces)
        if line not in lines:                           # the same code from another micro memory: once
            lines.append(line)
    open(path, 'w').write('\n'.join(lines) + '\n')
    print(f'{len(images)} program starts -> {path}')


def read_manifest(path, folder):
    """the program starts' micro memories, their code copied from the images"""
    srcs = page_images(folder)
    images = {}
    for line in open(path):
        p = line.split()
        if not p or p[0].startswith('#'):
            continue
        unit, entry, check = int(p[0]), int(p[1], 16), int(p[2], 16)
        micro, chk = bytearray(16384), 1469598103934665603
        for piece in p[3:]:
            at, name, off, n = piece.split(':')
            at, off, n = int(at, 16), int(off, 16), int(n, 16)
            if name not in srcs or off + n > len(srcs[name]):
                sys.exit(f'{path}: vu{unit} entry {entry:#x}: no image {name} of that size in {folder}')
            micro[at:at + n] = srcs[name][off:off + n]
            chk = fnv(srcs[name][off:off + n], chk)
        if chk != check:
            sys.exit(f'{path}: vu{unit} entry {entry:#x}: the code in {folder} differs '
                     '(another version of MGO1 than the one this build was made from)')
        micro = bytes(micro)
        images.setdefault((unit, entry, fnv(micro[:0x4000 if unit else 0x1000])), micro)
    return images


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dirs', nargs='*', help='dump folders (SIM_VU_STATE)')
    ap.add_argument('-o', '--out', required=True, help='folder of the generated files (vu_prog_*.cpp, vu_programs.cpp)')
    ap.add_argument('--no-fast', action='store_true', help='every program in the exact form (flags kept)')
    ap.add_argument('--no-static', action='store_true', help='the fast form for every VU1 program (its timing kept at run time)')
    ap.add_argument('--no-vu0-static', action='store_true', help='VU0\'s programs in the full form (the run-time pipeline)')
    ap.add_argument('--vu0-static', action='store_true', help='(the dedicated host\'s build, v141-host beta 13) the VU0 programs the static '
                    'form leaves in the full form (the status read or set inside, Q, a link or a register jump) in a static form too, '
                    'their status kept instruction by instruction')
    ap.add_argument('--no-clean', action='store_true', help='the static form without the clean operands (every operand through Op())')
    ap.add_argument('--lines', type=int, default=12000, help='lines per generated file')
    ap.add_argument('--manifest', help='the program starts from this manifest and --images, not from dumps')
    ap.add_argument('--write-manifest', help='write the dumps\' program starts as a manifest (with --images; with --manifest too: its starts and the dumps\')')
    ap.add_argument('--images', help='the folder of tools/ps2_images.py (mgs3_n_base.bin, <page>_<file>.bin)')
    a = ap.parse_args()
    global STATIC_NO_CLEAN
    STATIC_NO_CLEAN = a.no_clean
    if (a.manifest or a.write_manifest) and not a.images:
        sys.exit('--manifest and --write-manifest need --images')
    dumps = []
    for d in a.dirs:
        dumps += sorted(glob.glob(os.path.join(d, 'vus*_*.bin')))
    images = {}                     # (unit, entry, micro image) -> micro memory
    for f in dumps:
        b = open(f, 'rb').read()
        magic, unit, entry, top, itop, size = struct.unpack_from('<6I', b, 0)
        if magic != 0x31535556:
            continue
        vu = b[24:24 + size]
        micro = vu[size - 16384:]
        key = (unit, entry, fnv(micro[:0x4000 if unit else 0x1000]))
        images.setdefault(key, micro)
    if a.write_manifest:
        if a.manifest and os.path.exists(a.manifest):  # the manifest's program starts kept, the dumps' added
            for k, v in read_manifest(a.manifest, a.images).items():
                images.setdefault(k, v)
        write_manifest(images, a.images, a.write_manifest)
    if a.manifest:
        images = read_manifest(a.manifest, a.images)
    if not images:
        sys.exit('no program starts (dump folders or --manifest)')
    progs = {}                      # code key -> [unit, pcs, micro, entries]
    for (unit, entry, _), micro in sorted(images.items()):
        pcmask = 0x3ff8 if unit else 0xff8
        pcs, links = reachable(micro, entry, pcmask)
        code = b''.join(struct.pack('<I', pc) + micro[pc:pc + 8] for pc in pcs)
        key = (unit, fnv(code))
        if key not in progs:
            progs[key] = [unit, pcs, micro, [], {}, set()]
        if entry not in progs[key][3]:
            progs[key][3].append(entry)
            for pc, st in reachable.states.items():
                progs[key][4].setdefault(pc, set()).update(st)
            progs[key][5] |= reachable.edges
    # the fast form needs every VU1 program free of MAC / status reads
    def reads_flags(micro, pcs):
        for pc in pcs:
            lo, hi = struct.unpack_from('<II', micro, pc)
            if not hi & 0x80000000 and (lo >> 25) in FLAG_READS:
                return True
        return False
    vu1_reads_flags = any(reads_flags(micro, pcs) for (unit, pcs, micro, *_) in progs.values() if unit == 1)
    # the MAC a VU1 program leaves is read by no other when every MAC read sees its own program's
    mac_left_dead = all(mac_reads_own(micro, pcs, set(entries), edges) for (unit, pcs, micro, entries, st, edges) in progs.values() if unit == 1)
    os.makedirs(a.out, exist_ok=True)
    def put(name, text):                # only a changed file is written (its object stays up to date)
        path = os.path.join(a.out, name)
        if not (os.path.exists(path) and open(path).read() == text):
            with open(path + '.tmp', 'w') as f:     # a new file renamed over: a hard-linked gen/ keeps the old
                f.write(text)
            os.replace(path + '.tmp', path)
    head = ['// generated by recomp/vu2cpp.py from the game\'s own microprograms, do not edit',
            '#include "vu_micro.h"', 'namespace ps2 {', 'using namespace vux;']
    files, cur, table, decls, nfast, nstatic, nstatic0, nstatus0 = [], [], [], [], 0, 0, 0, 0
    for n, ((unit, h), (u, pcs, micro, entries, states, edges)) in enumerate(sorted(progs.items())):
        name = f'VuProg{n}'
        fast = unit == 1 and not a.no_fast and not vu1_reads_flags and fast_ok(micro, pcs)
        nfast += fast
        lines0 = None                                   # VU0: the static form with its flags, where it covers the program
        if unit == 0 and not (a.no_fast or a.no_static or a.no_vu0_static) and fast_ok(micro, pcs):
            lines0 = gen_program_static(name, micro, pcs, sorted(entries), states, keep_mac=True, unit=0)
        if lines0 is None and unit == 0 and a.vu0_static and not a.no_fast and fast_ok_status(micro, pcs):
            lines0 = gen_program_static(name, micro, pcs, sorted(entries), states, keep_mac=True, unit=0, status=True)
            nstatus0 += lines0 is not None
        if lines0 is not None:
            fast = 3                                    # (vu0.cpp VuRunCompiled: under the micro lock, the flags kept)
            nstatic0 += 1
            cur += lines0
        elif fast:
            lines = None if a.no_static else gen_program_static(name, micro, pcs, sorted(entries), states, keep_mac=not mac_left_dead)
            nstatic += lines is not None
            if lines is not None:
                fast = 2                                # the static form (vu0.cpp VuRunCompiled)
            cur += lines if lines is not None else gen_program_fast(name, micro, pcs, sorted(entries), states, keep_mac=not mac_left_dead)
        else:
            cur += gen_program(name, unit, micro, pcs, sorted(entries))
        decls.append(f'void {name}(MicroPos& P);')
        rng = ranges_of(pcs)
        chk = 1469598103934665603
        for lo_, hi_ in rng:
            chk = fnv(micro[lo_:hi_], chk)
        rname = f'kVuRanges{n}'
        decls.append(f'const uint16_t {rname}[][2] = {{' + ', '.join(f'{{{lo_}, {hi_}}}' for lo_, hi_ in rng) + '};')
        for e in sorted(entries):
            table.append((unit, e, name, rname, len(rng), chk, int(fast)))
        if len(cur) >= a.lines:
            files.append(cur); cur = []
    if cur:
        files.append(cur)
    for k, body in enumerate(files):
        put(f'vu_prog_{k}.cpp', '\n'.join(head + [''] + body + ['}  // namespace ps2', '']))
    for old in glob.glob(os.path.join(a.out, 'vu_prog_*.cpp')):
        if int(os.path.basename(old)[8:-4]) >= len(files):
            os.remove(old)
    tab = ['// generated by recomp/vu2cpp.py, do not edit', '#include "vu_micro.h"', 'namespace ps2 {'] + decls
    tab.append('const VuProgram kVuPrograms[] = {')
    for unit, e, name, rname, nr, chk, fast in sorted(table):
        tab.append(f'    {{{unit}, {e}, {nr}, {rname}, 0x{chk:016x}ull, &{name}, {fast}}},')
    if not table:
        tab.append('    {-1, 0, 0, nullptr, 0, nullptr, 0},')
    tab.append('};')
    tab.append(f'const int kVuProgramCount = {len(table)};')
    tab.append('}  // namespace ps2')
    put('vu_programs.cpp', '\n'.join(tab) + '\n')
    print(f'{len(images)} program starts, {len(progs)} programs ({nfast} fast, {nstatic} of them static, MAC left {"dead" if mac_left_dead else "kept"}; {nstatic0} VU0 programs static, {nstatus0} of them with the status kept), {len(table)} entries, {len(files)} files -> {a.out}'
          + (' (a VU1 program reads MAC / status: no fast form)' if vu1_reads_flags else ''))


if __name__ == '__main__':
    main()
