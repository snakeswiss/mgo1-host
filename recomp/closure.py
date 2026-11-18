#!/usr/bin/env python3
"""Function closure for the recompiler: starting from the ntmenu2 screen
range, follow calls through the overlay and the MGS3 PS2 base image and
list every function to recompile; stop at bridges (bridges.txt) and flag
functions that touch the hardware or make syscalls (bridge candidates).

    python closure.py <overlay.bin> <base_img.bin> bridges.txt out_functions.txt out_flags.txt
"""
import os, struct, sys

OVL_BASE, BASE_BASE = 0x2DA680, 0x100000
# another version (docs/VERSIONS.md): its base text's end, its pages' load address from their header
TEXT_END = int(os.environ.get('BASE_TEXT_END', '0x201b48'), 16)
UI_LO, UI_HI = 0x348000, 0x3A6000


def load(path, base):
    b = open(path, 'rb').read()
    return base, struct.unpack_from(f'<{len(b) // 4}I', b)


def main():
    # --page <name>: another stage page's overlay (a map, a resident page): its roots are
    # its command table handlers and every function of its text stored in data; the
    # default (ntmenu2) keeps the screen range and the network libraries as roots
    page = 'ntmenu2'
    if '--page' in sys.argv:
        k = sys.argv.index('--page'); page = sys.argv[k + 1]; del sys.argv[k:k + 2]
    # --alias <file>: the page's copies of functions the platform serves at ntmenu2's
    # addresses (tools/page_alias.py): bridges of that page
    alias = set()
    if '--alias' in sys.argv:
        k = sys.argv.index('--alias'); fa = sys.argv[k + 1]; del sys.argv[k:k + 2]
        if os.path.exists(fa):
            alias = {int(l.split()[0], 16) for l in open(fa) if l.strip() and not l.startswith('#')}
    global OVL_BASE
    if os.environ.get('OVL_FROM_HEADER'):
        OVL_BASE = struct.unpack_from('<I', open(sys.argv[1], 'rb').read(), 0)[0]
    images = [load(sys.argv[1], OVL_BASE), load(sys.argv[2], BASE_BASE)]
    hdr = images[0][1][:6]                # load address, image end, bss end, command table, text start, text end
    ovl_text_lo, ovl_text_hi, ovl_end = hdr[4], hdr[5], hdr[1]
    # code linked before the header's text start runs into it (ntmenu2's libraries from
    # 0x31AC80 below its 0x347F70): back 4 KB at a time while a window holds a jr ra,
    # then forward to the first stack decrement
    ow = images[0][1]
    k = (ovl_text_lo - OVL_BASE) // 4
    while k > 1024 and 0x03e00008 in ow[k - 1024:k]: k -= 1024
    while k < (ovl_text_lo - OVL_BASE) // 4 and not ((ow[k] & 0xffff8000) == 0x27bd8000): k += 1
    if OVL_BASE + 4 * k < ovl_text_lo:
        print(f'overlay code from {OVL_BASE + 4 * k:#x} (header text {ovl_text_lo:#x})')
        ovl_text_lo = OVL_BASE + 4 * k
    # --all: every function of the base text and of the page's text is a root (the native
    # build: the game's own main loop calls what it calls, nothing is left out)
    every = '--all' in sys.argv
    if every: sys.argv.remove('--all')
    # a bridge tagged [engine] is a service the mod's PC engine can take over (actors,
    # script, layouts, sound): recompiled too, the platform decides at run time
    lines = [l for l in open(sys.argv[3]) if l.strip() and not l.startswith('#')]
    bridges = {int(l.split()[0], 16) for l in lines if '[engine]' not in l}
    if page != 'ntmenu2':                 # the overlay-range bridges are ntmenu2's addresses
        bridges = {b for b in bridges if b < OVL_BASE}   # the page's copies of them (alias) are recompiled

    def word(a):
        for base, w in images:
            k = (a - base) // 4
            if 0 <= k < len(w): return w[k]
        return None

    def is_prologue(w): return w is not None and (w & 0xffff0000) == 0x27bd0000 and (w & 0x8000)
    # function starts: prologues and call targets, per image
    starts = set()
    JR_RA = 0x03e00008

    def terminator(x):
        op = x >> 26
        return (x == JR_RA or op == 2 or (op == 0 and (x & 63) == 8) or (x & 0xffff0000) == 0x10000000)

    def boundary(w, i):
        # the word before this one ends a function: a terminator + its delay
        # slot, then nothing but nop padding
        for back in (2, 3, 4):
            k = i - back
            if k < 0: return False
            if terminator(w[k]) and all(w[j] == 0 for j in range(k + 2, i)):
                return True
        return False
    # jumps are read from the text only: a data word that decodes as a jal must not make
    # an entry (0x6380AC, ntmenu2 data, "calls" 0x1C0834 inside 0x1C05D8)
    def in_text(a): return 0x100000 <= a < TEXT_END or ovl_text_lo <= a < ovl_text_hi
    jumps = {}
    for base, w in images:
        for i, x in enumerate(w):
            # a stack decrement starts a function only right after the end of
            # another (jr ra + delay slot, maybe nop padding): GCC often schedules
            # it a few instructions into the function
            if is_prologue(x) and boundary(w, i):
                starts.add(base + i * 4)
            if (x >> 26) in (2, 3) and in_text(base + i * 4):                # jal targets and tail jumps
                t = (x & 0x3ffffff) << 2
                jumps[t] = jumps.get(t, 0) + 1
    # entries reached through data (state tables, act / die pointers): a word or a
    # lui/addiu constant that points right after a jr ra + delay slot
    def after_ret(t):
        for bb, ww in images:
            k = (t - bb) // 4
            if 0 <= k < len(ww) and t % 4 == 0:
                return boundary(ww, k)
        return False
    # a j / jal target is a start, except one hit by a single word when the code runs
    # into it (a conditional branch or a call just before): a data word that decodes
    # as a jump must not cut a function in two (0x16F528 inside 0x16F420)
    def falls_in(t):
        y = word(t - 8)
        if y is None: return False
        op = y >> 26
        return op in (1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17) or op == 3 or (op == 0 and (y & 63) == 9)
    # a target jumped to by a single word, without a prologue nor a return before it, is weak:
    # a start, but one a branch of the code around it may show to be inside a function (below)
    weak = set()
    for t, n in jumps.items():
        if n == 1 and not is_prologue(word(t)) and not after_ret(t):
            if falls_in(t):
                continue
            weak.add(t)
        starts.add(t)
    # switch tables: the targets of `jr` through a table are case labels of the
    # function that jumps, not function starts
    case_labels = set()
    jal_targets = set()
    for base, w in images:
        for i, x in enumerate(w):
            if (x >> 26) == 3 and in_text(base + i * 4): jal_targets.add((x & 0x3ffffff) << 2)
    for base, w in images:
        for i, x in enumerate(w):
            if (x >> 26) != 0 or (x & 0x3f) != 8 or ((x >> 21) & 31) == 31:
                continue
            # follow the table address through lui / addiu / addu into the lw that
            # reads the target; any other write to a register forgets it
            hi = {}; table = None; from_lw = None
            for k in range(max(0, i - 12), i):
                y = w[k]; op = y >> 26; rt = (y >> 16) & 31; rs = (y >> 21) & 31; rd = (y >> 11) & 31
                imm = y & 0xffff; simm = imm - 0x10000 if imm & 0x8000 else imm
                if op == 0x0f:
                    hi[rt] = imm << 16
                elif op in (0x09, 0x19):                                    # addiu / daddiu
                    if rs in hi: hi[rt] = (hi[rs] + simm) & 0xffffffff; table = hi[rt]
                    else: hi.pop(rt, None)
                elif op == 0x00 and (y & 0x3f) in (0x21, 0x2d):             # addu / daddu: base + index
                    base_ = hi.get(rs, hi.get(rt))
                    if base_ is not None: hi[rd] = base_
                    else: hi.pop(rd, None)
                elif op == 0x23:                                            # lw
                    if rs in hi: from_lw = (hi[rs] + simm) & 0xffffffff
                    hi.pop(rt, None)
                elif op == 0x00:
                    hi.pop(rd, None)
                elif op not in (0x2b, 0x3f, 0x04, 0x05, 0x01, 0x02, 0x03):  # stores and branches write nothing
                    hi.pop(rt, None)
            if from_lw is not None: table = from_lw
            if table is None:
                continue
            here = base + i * 4
            for n in range(512):
                t = word(table + 4 * n)
                if t is None or t & 3 or not (here - 0x8000 <= t <= here + 0x8000): break
                case_labels.add(t)
    case_labels -= jal_targets
    data_starts = set()                   # function entries stored in data (callback and handler tables)
    for base, w in images:
        last = {}
        for i, x in enumerate(w):
            if 0x100000 <= x < max(0x645680, ovl_end) and after_ret(x):
                starts.add(x)
                data_starts.add(x)
            op = x >> 26
            if op == 0x0f: last[(x >> 16) & 31] = (i, (x & 0xffff) << 16)
            elif op in (0x09, 0x0d) and ((x >> 21) & 31) in last and i - last[(x >> 21) & 31][0] < 16:
                imm = x & 0xffff
                if op == 0x09 and imm & 0x8000: imm -= 0x10000
                t = (last[(x >> 21) & 31][1] + imm) & 0xffffffff
                if 0x100000 <= t < max(0x645680, ovl_end) and after_ret(t): starts.add(t)
    starts -= case_labels
    data_starts -= case_labels            # jump table words are case labels, not entries
    starts.discard(0)
    # a start taken from data (a word or a lui/addiu constant after a return) that a
    # branch of the code around it crosses is inside a function: GCC puts cold blocks
    # after a mid-function return and some unrelated data word may hold that address
    # (0x121338 in the pool allocator 0x1211F0, words of ntmenu2's network data)
    import bisect
    # (the EU ntmenu2's data word 0x34619C decodes as a jal into 0x43FEC0's middle: a weak target)
    firm = ({t for t, n in jumps.items()} | jal_targets) - weak
    for base, w in images:
        for i, x in enumerate(w):
            if is_prologue(x) and boundary(w, i): firm.add(base + 4 * i)
    roots_file = os.path.join(os.path.dirname(os.path.abspath(sys.argv[3])), 'roots.txt')
    root_list = []
    if os.path.exists(roots_file):
        for l in open(roots_file):
            if l.strip() and not l.startswith('#'):
                r = int(l.split()[0], 16)
                if page != 'ntmenu2' and r >= OVL_BASE: continue    # ntmenu2's own entries
                root_list.append(r)
    ordered = sorted(starts | set(root_list))
    firm_sorted = sorted(firm)
    crossed = set()
    for base, w in images:
        lo_txt, hi_txt = (0x100000, TEXT_END) if base == BASE_BASE else (ovl_text_lo, ovl_text_hi)
        for i, x in enumerate(w):
            a = base + 4 * i
            if not (lo_txt <= a < hi_txt): continue
            op = x >> 26
            if not (op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17) or (op == 1 and ((x >> 16) & 31) in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13))):
                continue
            imm = x & 0xffff
            t = a + 4 + ((imm - 0x10000 if imm & 0x8000 else imm) << 2)
            if not (lo_txt <= t < hi_txt): continue
            lo, hi = (a + 4, t) if t > a else (t + 4, a)
            # a branch to a real entry is a tail call; one over a real entry is data
            # that decodes as a branch: neither says where a function ends
            if t in firm: continue
            f = bisect.bisect_left(firm_sorted, min(a, t) + 4)
            if f < len(firm_sorted) and firm_sorted[f] < max(a, t): continue
            k = bisect.bisect_left(ordered, lo)
            while k < len(ordered) and ordered[k] <= hi:
                s = ordered[k]
                if s not in firm: crossed.add(s)
                k += 1
    starts -= crossed
    data_starts -= crossed
    print(f'{len(crossed)} data starts inside functions dropped')
    sorted_starts = sorted(starts)
    def end_of(a):
        k = bisect.bisect_right(sorted_starts, a)
        e = sorted_starts[k] if k < len(sorted_starts) else a + 4
        return min(e, a + 0x8000)
    if page == 'ntmenu2':
        todo = [s for s in sorted_starts if UI_LO <= s < UI_HI]
        # every function of the overlay's text stored in data: the network libraries'
        # tables (HTTP, SSL hashes and ciphers, sockets) and the handler tables the
        # screens call through (0x2F2A58: 0x43DF40..., by a type at +0x10; v115)
        todo += sorted(a for a in data_starts if ovl_text_lo <= a < ovl_text_hi)
    else:
        # the page's command / class table (0x2DA698, {code, handler} until 0) and
        # every function of its text stored in data (state and method tables)
        todo = []
        a = hdr[3]
        while True:
            c, h = word(a), word(a + 4)
            if not c or h is None: break
            if h >= OVL_BASE: todo.append(h)
            a += 8
        todo += sorted(x for x in data_starts if ovl_text_lo <= x < ovl_text_hi)
    if every:
        todo += [x for x in sorted_starts if (ovl_text_lo <= x < ovl_text_hi) or (0x100000 <= x < TEXT_END)]
    # every function of the base text stored in data: class method tables (the game
    # network's object and configuration block classes, 0x23C058, 0x23C5C8), script
    # command tables and callbacks are called through pointers, never by jal (v113)
    todo += sorted(a for a in data_starts if 0x100000 <= a < TEXT_END)
    # extra roots: code reached only through data (thread entries given to CreateThread ...);
    # one inside a function (a branch crosses it) is that function's code, not an entry
    for r in root_list:
        if r in crossed:
            print(f'root {r:#x} lies inside a function: left out')
            continue
        todo.append(r)
        if r not in starts:
            sorted_starts.append(r); sorted_starts.sort()
    def in_code(t):                       # text of the base (MD5 range) and of the overlay (from its load address)
        return 0x100000 <= t < TEXT_END or OVL_BASE <= t < ovl_text_hi
    seen, flags, system, outside = set(), {}, set(), set()
    visited = set()                       # each function is walked once, system ones included
    while todo:
        f = todo.pop()
        if f in visited or f in bridges: continue
        visited.add(f)
        if not in_text(f):                # data that decodes as code, met through a data word
            outside.add(f)
            continue
        seen.add(f)
        e = end_of(f)
        a = f
        lui = {}
        at_target = {}                    # a lui in a branch's delay slot also holds at the branch target
        while a < e:
            x = word(a)
            if x is None: break
            op = x >> 26
            for r, hi in at_target.pop(a, {}).items(): lui[r] = (a, hi)
            if op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17) or (op == 1 and ((x >> 16) & 31) in (0, 1, 2, 3)):
                ds = word(a + 4)
                if ds is not None and ds >> 26 == 0x0f:
                    imm = x & 0xffff
                    bt = a + 4 + ((imm - 0x10000 if imm & 0x8000 else imm) << 2)
                    if a < bt < e: at_target.setdefault(bt, {})[(ds >> 16) & 31] = (ds & 0xffff) << 16
            # function pointers built with lui + addiu/ori (callbacks, thread entries) are reached too
            if op == 0x0f:
                lui[(x >> 16) & 31] = (a, (x & 0xffff) << 16)
            elif op in (0x09, 0x0d) and ((x >> 21) & 31) in lui and a - lui[(x >> 21) & 31][0] < 64:
                imm = x & 0xffff
                if op == 0x09 and imm & 0x8000: imm -= 0x10000
                t = (lui[(x >> 21) & 31][1] + imm) & 0xffffffff
                if t in starts and t not in seen and t not in bridges: todo.append(t)
            if x & 0xfc00003f == 0x0000000c: flags.setdefault(f, set()).add('syscall')
            if op == 0x0f and 0x1000 <= (x & 0xffff) <= 0x1fff:
                # hardware only when that register then addresses memory (MD5's 0x10325476 is a constant)
                r = (x >> 16) & 31
                for k in range(1, 6):
                    y = word(a + 4 * k)
                    if y is None: break
                    yop, yrs = y >> 26, (y >> 21) & 31
                    if yop in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37, 0x28, 0x29, 0x2b, 0x3f, 0x1e, 0x1f) and yrs == r:
                        flags.setdefault(f, set()).add(f'hw lui {x & 0xffff:#x}')
                        break
                    if ((y >> 16) & 31) == r and yop not in (0x28, 0x29, 0x2b, 0x3f): break
            if op == 0x12: flags.setdefault(f, set()).add('cop2')
            if op == 0x10 and ((x >> 21) & 31) == 0x10 and (x & 63) == 0x18: flags.setdefault(f, set()).add('eret')
            if op == 3:
                t = (x & 0x3ffffff) << 2
                if t not in seen and t not in bridges and word(t) is not None: todo.append(t)
            # jumps and branches out of the function are calls of the target (tail calls)
            tgt = None
            if op == 2:                                          # a `j` out of the function: a tail call
                tgt = (a & 0xf0000000) | ((x & 0x3ffffff) << 2)
            elif op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17) or (op == 1 and ((x >> 16) & 31) in (0, 1, 2, 3)):
                imm = x & 0xffff
                tgt = a + 4 + ((imm - 0x10000 if imm & 0x8000 else imm) << 2)
                if tgt not in jal_targets: tgt = None          # a branch out only reaches a real function
            if tgt is not None and not (f <= tgt < e) and in_code(tgt) and tgt in starts and tgt not in visited and tgt not in bridges:
                todo.append(tgt)
            a += 4
        # a function that does not end on a jump runs on into the next one (when that
        # one is a real function: others are boundaries taken from data)
        def is_term(y):
            if y is None: return False
            yop = y >> 26
            return yop == 2 or (yop == 0 and (y & 0x3f) == 8) or (yop == 4 and ((y >> 16) & 31) == 0 and ((y >> 21) & 31) == 0)
        if in_code(f) and in_code(e) and e in jal_targets and e not in visited and e not in bridges:
            k = e - 4
            while k > f and word(k) == 0: k -= 4          # nop padding after the last instruction
            if not (is_term(word(k)) or is_term(word(k - 4))): todo.append(e)
        # system function: served by the platform, not recompiled. VU0 macro code ('cop2')
        # is recompiled (vu0.cpp), and so is code on hardware registers ('hw lui': the
        # runtime's R/W serve them, ps2rt.cpp); syscall stubs and exception returns stay out
        # a syscall stub (li $v1, n; syscall; jr $ra) is served by the platform by its address;
        # a syscall inside other code is recompiled (Syscall: the number in $v1, e.g. crt0's)
        def is_stub(g):
            ws = [word(g + 4 * k) for k in range(6)]
            for k in range(3):
                if ws[k] == 0x0000000c:
                    return any(w == JR_RA for w in ws[k + 1:k + 3] if w is not None)
            return False
        fl = {x for x in flags.get(f, ()) if x != 'cop2' and not x.startswith('hw lui')}
        if 'syscall' in fl and not is_stub(f): fl.discard('syscall')
        if fl:
            seen.discard(f)
            system.add(f)
    with open(sys.argv[4], 'w') as fo:
        for f in sorted(seen): fo.write(f'{f:#x} {end_of(f):#x}\n')
    with open(sys.argv[5], 'w') as fo:
        for f in sorted(flags): fo.write(f'{f:#x} {" ".join(sorted(flags[f]))}\n')
    print(f'{len(seen)} functions to recompile, {len(system)} system functions left to the platform, '
          f'{len(outside)} entries outside the text left out')


if __name__ == '__main__':
    main()
