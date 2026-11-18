#!/usr/bin/env python3
"""IOP modules (IRX: relocatable MIPS ELF, type 0xFF80): loaded at a base address as the IOP
kernel loads them (sections, relocations, bss), with their import stubs and export tables.

    python3 irx.py info <module.irx> [base hex]            sections, imports, exports, entry
    python3 irx.py image <module.irx> <base hex> <out.bin>  the loaded image (text, data, bss)
    python3 irx.py dis <module.irx> <base hex> <from hex> [<to hex>|+<len hex>]

As a library: load(path, base) -> Module(image, base, entry, imports {addr: (lib, index)},
exports {lib: [addresses]}, text_end, gp).
"""
import struct
import sys

R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_GPREL16 = 2, 4, 5, 6, 7


class Module:
    pass


def load(path_or_bytes, base):
    d = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, 'rb').read()
    assert d[:4] == b'\x7fELF', 'not an ELF'
    e_type, = struct.unpack_from('<H', d, 0x10)
    entry, = struct.unpack_from('<I', d, 0x18)
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x2e)
    secs = [list(struct.unpack_from('<10I', d, shoff + i * shentsize)) for i in range(shnum)]
    strtab = secs[shstrndx]

    def sname(o):
        s = d[strtab[4] + o:]
        return s[:s.index(b'\0')].decode()
    size = 0
    for s in secs:
        nm, typ, flags, addr, off, sz = s[:6]
        if flags & 2 and typ in (1, 8):                  # allocated: PROGBITS / NOBITS
            size = max(size, addr + sz)
    img = bytearray(size)
    text_end = 0
    for s in secs:
        nm, typ, flags, addr, off, sz = s[:6]
        if flags & 2 and typ == 1:
            img[addr:addr + sz] = d[off:off + sz]
        if sname(nm) == '.text':
            text_end = addr + sz
    # relocations (REL): the image's addresses are module offsets, base added
    for s in secs:
        nm, typ, flags, addr, off, sz, link, info = s[:8]
        if typ != 9:
            continue
        hi = []
        for k in range(sz // 8):
            r_off, r_info = struct.unpack_from('<II', d, off + 8 * k)
            rt = r_info & 0xff
            w, = struct.unpack_from('<I', img, r_off)
            if rt == R_MIPS_32:
                w = (w + base) & 0xffffffff
            elif rt == R_MIPS_26:
                t = ((w & 0x3ffffff) << 2) + base
                w = (w & 0xfc000000) | ((t >> 2) & 0x3ffffff)
            elif rt == R_MIPS_HI16:
                hi.append(r_off)
                continue
            elif rt == R_MIPS_LO16:
                lo = w & 0xffff
                slo = lo - 0x10000 if lo & 0x8000 else lo
                for h in hi:                              # each HI16 before this LO16: the full value
                    hw, = struct.unpack_from('<I', img, h)
                    v = ((hw & 0xffff) << 16) + slo + base
                    hw = (hw & 0xffff0000) | (((v + 0x8000) >> 16) & 0xffff)
                    struct.pack_into('<I', img, h, hw)
                hi = []
                v = slo + base
                w = (w & 0xffff0000) | (v & 0xffff)
            else:
                raise ValueError(f'relocation type {rt} at {r_off:#x}')
            struct.pack_into('<I', img, r_off, w)
    m = Module()
    m.image, m.base, m.entry, m.text_end, m.size = bytes(img), base, base + entry, base + text_end, size
    # import tables (0x41E00000): stubs "jr ra; addiu zero, zero, index", patched by the kernel
    m.imports, m.import_libs = {}, []
    i = 0
    while True:
        i = img.find(struct.pack('<I', 0x41e00000), i)
        if i < 0:
            break
        lib = img[i + 12:i + 20].rstrip(b'\0').decode('latin1')
        j = i + 20
        while j + 8 <= len(img):
            w1, w2 = struct.unpack_from('<II', img, j)
            if w1 == 0x03e00008 and (w2 >> 16) == 0x2400:
                m.imports[base + j] = (lib, w2 & 0xffff)
                j += 8
            else:
                break
        m.import_libs.append((base + i, lib))
        i += 4
    # export tables (0x41C00000): function pointers after the header (entries 0..3: start, stop, ...)
    m.exports = {}
    i = img.find(struct.pack('<I', 0x41c00000))
    if i >= 0:
        lib = img[i + 12:i + 20].rstrip(b'\0').decode('latin1')
        fns = []
        j = i + 20
        while j + 4 <= len(img):
            w, = struct.unpack_from('<I', img, j)
            nxt, = struct.unpack_from('<I', img, j + 4) if j + 8 <= len(img) else (0,)
            if w == 0 and (nxt == 0 or nxt == 0x03e00008 or nxt == 0x41e00000):
                break                                  # the table's end (a zero before the next table)
            fns.append(w)
            j += 4
        m.exports[lib] = fns
        m.export_table = base + i
    return m


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
    m = load(path, base)
    if cmd == 'info':
        print(f'image {m.size:#x} bytes at {m.base:#x}, entry {m.entry:#x}, text ends {m.text_end:#x}')
        libs = {}
        for a, (lib, n) in sorted(m.imports.items()):
            libs.setdefault(lib, []).append((n, a))
        for lib, v in libs.items():
            print(f'  import {lib}: ' + ' '.join(f'{n}@{a:x}' for n, a in v))
        for lib, fns in m.exports.items():
            print(f'  export {lib}: ' + ' '.join(f'{k}={f:x}' for k, f in enumerate(fns)))
    elif cmd == 'image':
        open(sys.argv[4], 'wb').write(m.image)
        print(f'{sys.argv[4]}: {m.size:#x} bytes at {m.base:#x}')
    elif cmd == 'dis':
        from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
        md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 + CS_MODE_LITTLE_ENDIAN)
        a = int(sys.argv[4], 16)
        e = sys.argv[5] if len(sys.argv) > 5 else '+0x100'
        e = a + int(e[1:], 16) if e.startswith('+') else int(e, 16)
        for x in range(a, e, 4):
            o = x - m.base
            w, = struct.unpack_from('<I', m.image, o)
            note = ''
            if x in m.imports:
                note = f'   ; import {m.imports[x][0]} #{m.imports[x][1]}'
            ins = list(md.disasm(m.image[o:o + 4], x))
            txt = f'{ins[0].mnemonic} {ins[0].op_str}' if ins else f'.word {w:#010x}'
            if ins and ins[0].mnemonic == 'jal':
                t = int(ins[0].op_str, 16)
                if t in m.imports:
                    note = f'   ; -> {m.imports[t][0]} #{m.imports[t][1]}'
            print(f'  {x:08x}: {w:08x}  {txt}{note}')


if __name__ == '__main__':
    main()
