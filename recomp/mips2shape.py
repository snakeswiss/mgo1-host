#!/usr/bin/env python3
"""Shapes: one recompiled body for every function whose code is the same once its relocated
fields are set aside (docs/SHAPES.md).

A function's shape is its instruction words with the 16-bit field of every I-type instruction
masked, except the stack's offsets (rs = sp), and the targets of j / jal masked (a j inside the
function kept as its offset); a variant of recomp/variants.txt inside it is part of its shape.
Each shape becomes `void S_<id>(Ctx& c, uint32_t A)`: A is the address of the instance being
run, the masked fields are read from its own code in PS2 memory (IMM, UIMM, HI, TGT: ps2rt.h),
labels and pc values are offsets from A. The tables map each function's address (the base
image's, each page's) to its shape.

    python3 recomp/mips2shape.py --base <mgs3_n_base.bin> --base-functions recomp/closure_base.txt
        --page <name> <image.bin> <functions.txt> [<alias.txt>] ... --out MGO1Loader/gen
"""
import argparse, hashlib, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mips2cpp as M

STRUCT_OPS = {0, 1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17, 0x10, 0x11, 0x12, 0x13, 0x1c}
BASE = 0x100000


def masked(w):
    """the instruction's 16-bit field is read from the instance (all I-type ones but the stack's)"""
    op = w >> 26
    if op in STRUCT_OPS or op in (2, 3):
        return False
    return op == 0x0f or ((w >> 21) & 31) != 29


def shape_key(words, start, variants):
    n = len(words)
    out = []
    for k, w in enumerate(words):
        op = w >> 26
        if op in (2, 3):
            t = ((start + 4 * k) & 0xf0000000) | ((w & 0x3ffffff) << 2)
            out.append(('j', t - start) if op == 2 and start <= t < start + 4 * n else w & 0xfc000000)
        elif masked(w):
            out.append(w & 0xffff0000)
        else:
            out.append(w)
    return repr((out, sorted((a - start, k, v, i) for a, lst in variants.items() for k, v, i in lst)))


def shape_id(key):
    return hashlib.md5(key.encode()).hexdigest()[:12]


def emit_shape(out, sid, start, words, variants):
    """the shape's body from one instance (start, words), as mips2cpp.emit_fn with its fields
    read from the instance (A)"""
    n = len(words)
    end = start + 4 * n
    ins = [(start + 4 * k, words[k]) for k in range(n)]
    code, targets = {}, set()
    uses_variant = False
    uses_w = any(masked(w) or (w >> 26) in (2, 3) for _, w in ins)
    for a, w in ins:
        off = a - start
        kind, c = M.gen_ins(a, w, None, None, M.Par(a, w, off, masked(w)))
        for vk, vv, vid in variants.get(a, []):
            if kind is not None:
                raise SystemExit(f'variant at {a:#x}: not a straight instruction')
            if vk == 'word':
                k2, c2 = M.gen_ins(a, vv, None, None, M.Par(a, vv, off, False))
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
        if kind and kind[0] == 'br' and start <= kind[2] < end: targets.add(kind[2])
        if kind and kind[0] == 'j' and start <= kind[1] < end: targets.add(kind[1])
    if uses_variant:
        out.append('extern unsigned char gVariant[64];')
    out.append(f'void S_{sid}(Ctx& c, uint32_t A) {{')
    if uses_w:
        out.append('  const uint8_t* W = WPTR(A);')
    L = lambda x: f'L_{x - start:x}'
    has_jr = any(k and k[0] == 'jr' and k[1] != 31 for k, _ in code.values())
    skip = set()

    def goto(t, a, off, far):
        if start <= t < end:
            return '{ ' + ('BACKEDGE(); ' if t <= a else '') + f'goto {L(t)}; }}'
        return f'{{ Call(c, {far}); return; }}'
    for a, w in ins:
        if a in skip:
            continue
        off = a - start
        kind, c = code[a]
        lab = f'{L(a)}: ' if (a in targets or has_jr) else ''
        if kind is None:
            out.append(f'  {lab}{c}')
            continue
        dly = code.get(a + 4, (None, ''))
        dcode = dly[1] if dly[0] is None else f'/* control in delay slot at +{off + 4:#x} */'
        skip.add(a + 4)
        tail = f'  {L(a + 4)}: ;' if (a + 4 in targets or has_jr) else None
        if kind[0] == 'br':
            _, cond, t, likely, link = kind
            jump = goto(t, a, off, f'A + {t - start:#x}')
            linkc = f'{M.R(31)} = A + {off + 8:#x};' if link else ''
            if likely:
                out.append(f'  {lab}if ({cond}) {{ {linkc} {dcode} {jump} }}')
            else:
                out.append(f'  {lab}{{ bool t_ = {cond}; {linkc} {dcode} if (t_) {jump} }}')
        elif kind[0] == 'j':
            out.append(f'  {lab}{{ {dcode} {goto(kind[1], a, off, f"TGT({off:#x})")} }}')
        elif kind[0] == 'jal':
            out.append(f'  {lab}{{ {dcode} {M.R(31)} = A + {off + 8:#x}; Call(c, TGT({off:#x})); }}')
        elif kind[0] == 'jr':
            rs = kind[1]
            if rs == 31:
                out.append(f'  {lab}{{ {dcode} return; }}')
            else:
                cases = ' '.join(f'case {x - start:#x}: goto {L(x)};' for x, _ in ins)
                out.append(f'  {lab}{{ uint32_t t_ = (uint32_t){M.R(rs)}; {dcode} switch (t_ - A) {{ {cases} default: Call(c, t_); return; }} }}')
        elif kind[0] == 'jalr':
            _, rd, rs = kind
            out.append(f'  {lab}{{ uint32_t t_ = (uint32_t){M.R(rs)}; {dcode} {M.R(rd)} = A + {off + 8:#x}; Call(c, t_); }}')
        if tail:
            out.append(tail)
    last = ins[-1][0] if ins else start
    lk = code.get(last - 4, (None, ''))[0] if last - 4 in code else None
    if not (lk and lk[0] in ('j', 'jr')) and not (code.get(last, (None, ''))[0] and code[last][0][0] in ('j', 'jr')):
        out.append(f'  Call(c, A + {4 * n:#x}); return;')
    out.append('}')


def load_image(path, base=None):
    blob = open(path, 'rb').read()
    words = struct.unpack_from(f'<{len(blob) // 4}I', blob)
    if base is None:
        base = words[0]                  # a page overlay: its header's load address
    return base, words


def load_functions(path):
    fns = []
    for line in open(path):
        p = line.split()
        if len(p) >= 2 and not line.startswith('#'):
            fns.append((int(p[0], 16), int(p[1], 16)))
    return sorted(fns)


def read_map(path):
    """tools/versions.py's pairs: page -> [(US start, the other's start, same shape)]"""
    pairs, page = {}, None
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('['):
            page = line[1:line.index(']')]
            continue
        if page == 'data':
            continue
        p = line.split()
        pairs.setdefault(page, []).append((int(p[0], 16), int(p[1], 16), len(p) < 5, int(p[3])))
    return pairs


def moved_variants(page, variants, us_functions, pairs, us_imgs=None, v_imgs=None):
    """US variants of a page placed in another version: the site's function paired with one of the
    same shape, the site at the same offset; in a changed function, at the instruction the two
    functions' instructions line up with (their masked words); the others are reported"""
    import bisect, difflib
    starts = sorted(us_functions)
    ends = dict(us_functions.items()) if isinstance(us_functions, dict) else {}
    where = {}
    for us, o, same, ol in pairs.get(page, []) + (pairs.get('base', []) if page != 'base' else []):
        where[us] = (o, same, ol)
    out, lost = {}, []

    def words(imgs, s, n):
        for b, w in imgs:
            k = (s - b) // 4
            if 0 <= k and k + n <= len(w):
                return w[k:k + n]
        return None

    def mask(w):
        op = w >> 26
        if op in (2, 3): return w & 0xfc000000
        return w & 0xffff0000 if masked(w) else w
    for addr, lst in variants.items():
        k = bisect.bisect_right(starts, addr) - 1
        f = starts[k] if k >= 0 else None
        if f is None or f not in where:
            lost.append(addr)
            continue
        o, same, ol = where[f]
        if same:
            out[o + (addr - f)] = lst
            continue
        uw = words(us_imgs or [], f, (ends.get(f, f) - f) // 4) if ends else None
        vw = words(v_imgs or [], o, ol)
        if not uw or not vw:
            lost.append(addr)
            continue
        i = (addr - f) // 4
        sm = difflib.SequenceMatcher(None, [mask(w) for w in uw], [mask(w) for w in vw], autojunk=False)
        j = None
        for a0, b0, n in sm.get_matching_blocks():
            if a0 <= i < a0 + n:
                j = b0 + (i - a0)
        if j is None:
            lost.append(addr)
            continue
        out[o + 4 * j] = lst
    return out, lost


def own_variants(page, vname, vv):
    """a version's own sites of a variant (variants.txt lines of the page <page>:<version>, at that
    version's addresses) added to the ones moved from the US program"""
    placed = []
    for addr, lst in M.load_variants(f'{page}:{vname}').items():
        vv.setdefault(addr, []).extend(lst)
        placed.append((page, addr))
    return placed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', required=True, help='mgs3_n_base.bin')
    ap.add_argument('--base-functions', required=True)
    ap.add_argument('--page', nargs='+', action='append', default=[], metavar='NAME IMAGE FUNCTIONS [ALIAS]')
    # other versions (docs/VERSIONS.md): NAME, their images' folder, their closures' folder, tools/versions.py's map
    ap.add_argument('--version', nargs=4, action='append', default=[], metavar=('NAME', 'IMAGES', 'FUNCTIONS', 'MAP'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--buckets', type=int, default=256)
    a = ap.parse_args()
    base_img = load_image(a.base, BASE)
    shapes = {}                           # id -> (start, words, variants): the first instance
    order = []
    tables = []                           # (version, name, [(addr, id)], alias path)

    def instances(name, imgs, fns, lo, hi, variants):
        rows = []
        for s, e in fns:
            if not (lo <= s < hi):
                continue
            for b, w in imgs:
                k = (s - b) // 4
                if 0 <= k and (e - b) // 4 <= len(w):
                    words = w[k:(e - b) // 4]
                    break
            else:
                raise SystemExit(f'{name}: {s:#x} is in no image')
            v = {x: variants[x] for x in variants if s <= x < e}
            key = shape_key(words, s, v)
            sid = shape_id(key)
            old = shapes.get(sid)
            if old is None:
                shapes[sid] = (s, words, v, key)
                order.append(sid)
            elif old[3] != key:
                raise SystemExit(f'shape id collision {sid}')
            rows.append((s, sid))
        return rows
    base_fns = load_functions(a.base_functions)
    first_page = a.page[0] if a.page else None
    # the base: its functions below the first page's load address
    ovl_lo = load_image(first_page[1])[0] if first_page else 0x2DA680
    tables.append(('US', 'base', instances('base', [base_img], base_fns, BASE, ovl_lo, M.load_variants('base')), None))
    us_page_fns = {'base': {s: e for s, e in base_fns if s < ovl_lo}}
    us_page_imgs = {'base': [base_img]}
    for p in a.page:
        name, image, functions = p[0], p[1], p[2]
        alias = p[3] if len(p) > 3 else None
        ob, ow = load_image(image)
        fl = load_functions(functions)
        us_page_fns[name] = {**us_page_fns['base'], **{s: e for s, e in fl if s >= ob}}
        us_page_imgs[name] = [(ob, ow), base_img]
        rows = instances(name, [(ob, ow), base_img], fl, ob, ob + 4 * len(ow), M.load_variants(name))
        tables.append(('US', name, rows, alias))
    # the other versions: their own functions, the US variants moved to their sites
    for vname, vimages, vfuns, vmap in a.version:
        pairs = read_map(vmap)
        vbase = load_image(os.path.join(vimages, 'mgs3_n_base.bin'), BASE)
        vbase_fns = load_functions(os.path.join(vfuns, 'closure_base.txt'))
        lost_all = []
        vv, lost = moved_variants('base', M.load_variants('base'), us_page_fns['base'], pairs, us_page_imgs['base'], [vbase])
        lost_all += [('base', x) for x in lost]
        own_all = own_variants('base', vname, vv)
        vpage0 = None
        for p in a.page:
            img = [f for f in os.listdir(vimages) if f.startswith(p[0] + '_') and f.endswith('.bin')]
            if img:
                vpage0 = load_image(os.path.join(vimages, sorted(img)[0]))[0]
                break
        tables.append((vname, 'base', instances(vname + ' base', [vbase], vbase_fns, BASE, vpage0 or 0x2DA680, vv), None))
        for p in a.page:
            name = p[0]
            img = [f for f in os.listdir(vimages) if f.startswith(name + '_') and f.endswith('.bin')]
            fl_path = os.path.join(vfuns, 'closure_functions.txt' if name == 'ntmenu2' else f'closure_{name}.txt')
            if not img or not os.path.exists(fl_path):
                print(f'{vname}: no {name}')
                continue
            ob, ow = load_image(os.path.join(vimages, sorted(img)[0]))
            vv, lost = moved_variants(name, M.load_variants(name), us_page_fns[name], pairs, us_page_imgs[name], [(ob, ow), vbase])
            lost_all += [(name, x) for x in lost]
            own_all += own_variants(name, vname, vv)
            rows = instances(f'{vname} {name}', [(ob, ow), vbase], load_functions(fl_path), ob, ob + 4 * len(ow), vv)
            tables.append((vname, name, rows, None))
        if lost_all:
            print(f'{vname}: variant sites not placed (their function changed): ' + ' '.join(f'{p}:{x:#x}' for p, x in lost_all))
        if own_all:
            print(f'{vname}: variant sites of its own (variants.txt <page>:{vname}): ' + ' '.join(f'{p}:{x:#x}' for p, x in own_all))
    os.makedirs(a.out, exist_ok=True)
    head = ['// generated by mips2shape.py, do not edit', '#pragma once', '#include "ps2rt.h"', 'namespace ps2 {']
    head += [f'void S_{sid}(Ctx& c, uint32_t A);' for sid in order]
    head.append('}  // namespace ps2')
    written = {'s_decls.h'}
    open(os.path.join(a.out, 's_decls.h'), 'w').write('\n'.join(head) + '\n')
    buckets = {}
    for sid in order:
        buckets.setdefault(int(sid[:2], 16) * a.buckets // 256, []).append(sid)
    words_total = 0
    for b, ids in sorted(buckets.items()):
        out = ['// generated by mips2shape.py, do not edit', '#include "s_decls.h"', 'namespace ps2 {']
        for sid in sorted(ids):
            s, words, v, _ = shapes[sid]
            words_total += len(words)
            emit_shape(out, sid, s, words, v)
        out.append('}  // namespace ps2')
        fn = f's_{b:02x}.cpp'
        written.add(fn)
        open(os.path.join(a.out, fn), 'w').write('\n'.join(out) + '\n')
    count = 0
    for version, name, rows, alias in tables:
        count += len(rows)
        tab = ['// generated by mips2shape.py, do not edit', '#include "s_decls.h"', 'namespace ps2 {']
        tab.append('static const FnEntry kFns[] = {')
        tab += [f'  {{{s:#x}, S_{sid}}},' for s, sid in rows]
        tab.append('};')
        al = []
        if alias and os.path.exists(alias):
            al = sorted(tuple(int(x, 16) for x in l.split()[:2]) for l in open(alias) if l.strip() and not l.startswith('#'))
        if al:
            tab.append('static const AliasEntry kAlias[] = {')
            tab += [f'  {{{p:#x}, {q:#x}}},' for p, q in al]
            tab += ['};', f'static OverlayRegistration kReg("{version}", "{name}", kFns, {len(rows)}, kAlias, {len(al)});']
        else:
            tab.append(f'static OverlayRegistration kReg("{version}", "{name}", kFns, {len(rows)});')
        tab.append('}  // namespace ps2')
        fn = f'g_{name}_table.cpp' if version == 'US' else f'g_{version.lower()}_{name}_table.cpp'
        written.add(fn)
        open(os.path.join(a.out, fn), 'w').write('\n'.join(tab) + '\n')
    # each other version's map to the US program (the platform's addresses): tools/versions.py's
    for vname, vimages, vfuns, vmap in a.version:
        tab = ['// generated by mips2shape.py from tools/versions.py\'s map, do not edit', '#include "ps2rt.h"', 'namespace ps2 {']
        imgs, runs, page, gp = [], [], None, (0, 0)
        cur = []
        for line in open(vmap):
            line = line.strip()
            if line.startswith('# ') and ' gp ' in line:
                g = line.split(' gp ')[1].split(' -> ')
                gp = (int(g[0], 16), int(g[1], 16))
                continue
            if not line or line.startswith('#'):
                continue
            if line.startswith('['):
                page = line[1:line.index(']')]
                if page != 'data':
                    lo = line[line.index(']') + 1:].split()
                    imgs.append([page, int(lo[0], 16), int(lo[1], 16), []])
                continue
            p = line.split()
            if page == 'data':
                runs.append((int(p[0], 16), int(p[1], 16), int(p[2], 16), int(p[3])))
            else:
                imgs[-1][3].append((int(p[0], 16), int(p[1], 16), int(p[2]), int(p[3]), 1 if len(p) < 5 else 0))
        for k, (pg, ulo, vlo, prs) in enumerate(imgs):
            tab.append(f'static const VersionPair kPairs{k}[] = {{')
            tab += [f'  {{{u:#x}, {v:#x}, {ul}, {vl}, {same}}},' for u, v, ul, vl, same in prs]
            tab.append('};')
        tab.append('static const VersionImageMap kImages[] = {')
        tab += [f'  {{"{pg}", {ulo:#x}, {vlo:#x}, kPairs{k}, {len(prs)}}},' for k, (pg, ulo, vlo, prs) in enumerate(imgs)]
        tab.append('};')
        tab.append('static const VersionRun kRuns[] = {')
        tab += [f'  {{{lo:#x}, {hi:#x}, {d}, {n}}},' for lo, hi, d, n in runs]
        tab.append('};')
        tab.append(f'static VersionMapRegistration kMap("{vname}", {gp[0]:#x}, {gp[1]:#x}, kImages, {len(imgs)}, kRuns, {len(runs)});')
        tab.append('}  // namespace ps2')
        fn = f'g_{vname.lower()}_map.cpp'
        written.add(fn)
        open(os.path.join(a.out, fn), 'w').write('\n'.join(tab) + '\n')
    open(os.path.join(a.out, 'shapes.txt'), 'w').write('\n'.join(sorted(written)) + '\n')
    print(f'{count} functions of {len(tables)} tables in {len(order)} shapes ({words_total} words) in {len(buckets)} files')


if __name__ == '__main__':
    main()
