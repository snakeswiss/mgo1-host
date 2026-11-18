#!/usr/bin/env python3
"""Prepare the PS2 code images the native ntmenu2 runs from, out of the
user's own MGO1 disc (nothing of Konami's is shipped):

    python ps2_images.py <disc> <game>
    python ps2_images.py <MGS3_N.ELF> <STAGE_N.DAT> <game>

<disc> is the MGO1 disc's ISO image, the disc in a drive, or a folder with its
files. Writes <game>\\mgo1\\ps2\\mgs3_n_base.bin (the base image, the zlib stream
at 0x1004 of MGS3_N.ELF, loaded at 0x100000) and, for every stage page that
carries code, <page>_<file>.bin (its BINARY section, loaded at 0x2DA680 when
the page is the current stage): ntmenu2_046341.bin for the menu, the maps for
the match. The disc must be one of the five versions (docs/VERSIONS.md), as it came.
    python ps2_images.py --which <disc>   the version of its program: US EU JP JPS KR
"""
import hashlib, os, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_dat

BASE_MD5 = None      # filled after the first measured run


def loader_fixup(elf, base):
    """The ELF's loader stub inflates the image to 0x100000, then XORs the word at 0x100004 with the
    sum of a range of its own words before it jumps to 0x100024: the image as the game sees it in
    memory. Each version's stub is the same code at its own place (docs/VERSIONS.md): the range and
    the image's pointer are read from its instructions (+0x90/+0x98 and +0x94/+0x9C the range)."""
    import struct
    if len(elf) < 0x34:
        return base
    u = lambda o: struct.unpack_from('<I', elf, o)[0]
    entry, ph = u(0x18), u(0x1C)
    off, va = u(ph + 4), u(ph + 8)
    w = lambda k: u(off + entry - va + k)
    if (w(0x1C) >> 16) != 0x3C0E or (w(0x38) >> 16) != 0x8DD1:
        return base
    s16 = lambda x: x - 0x10000 if x & 0x8000 else x
    lo = ((w(0x90) & 0xffff) << 16) + s16(w(0x98) & 0xffff)
    hi = ((w(0x94) & 0xffff) << 16) + s16(w(0x9C) & 0xffff)
    total = 0
    for a in range(lo, hi, 4):
        total = (total + u(off + a - va)) & 0xFFFFFFFF
    x = struct.unpack_from('<I', base, 4)[0] ^ total
    return base[:4] + struct.pack('<I', x) + base[8:]


# md5 of the base image, per program (as its loader stub makes it)
BASES = {'6cfa9adbcf2166b3d99d657bf2b8b378': 'US', 'cfef3ab89a7c49999776445886e44b89': 'EU',
         '87169bd6176ac26d396fee8638209ec8': 'JP', '562e90edeb7a4b234c556e9549454766': 'JPS',
         '6ecbbffe27ec60e4561ec1d99d5aa635': 'KR'}


def from_disc(disc, tmp):
    """MGS3_N.ELF and STAGE_N.DAT of a disc (an ISO image or a folder): paths to read them from"""
    if os.path.isfile(disc):
        import disc_copy
        iso = disc_copy.Iso(disc)
        paths = []
        for name in ('MGS3_N.ELF', 'STAGE_N.DAT'):
            if name not in iso.files:
                sys.exit(f'no {name} in {disc}')
            dst = os.path.join(tmp, name)
            iso.copy(name, dst)
            paths.append(dst)
        return paths
    found = {}
    for d, _, files in os.walk(disc):
        for f in files:
            found.setdefault(f.split(';')[0].upper(), os.path.join(d, f))
    missing = [n for n in ('MGS3_N.ELF', 'STAGE_N.DAT') if n not in found]
    if missing:
        sys.exit(f'not in {disc}: ' + ', '.join(missing))
    return [found['MGS3_N.ELF'], found['STAGE_N.DAT']]


def base_of(elf_bytes):
    """the base image of an MGS3_N.ELF (inflated, the loader stub's fix applied), None when absent"""
    e = elf_bytes
    for off in (0x1004,) + tuple(range(0, min(len(e), 0x4000), 4)):
        if e[off:off + 1] == b'\x78':
            try:
                d = zlib.decompressobj().decompress(e[off:])
            except zlib.error:
                continue
            if len(d) > 1_000_000:
                return loader_fixup(e, d)
    return None


def main():
    # --which <disc>: the version of a disc's program (US EU JP JPS KR), for build_all.sh
    if len(sys.argv) == 3 and sys.argv[1] == '--which':
        import tempfile
        tmp = tempfile.mkdtemp(prefix='mgo1_')
        elf = from_disc(sys.argv[2], tmp)[0] if not sys.argv[2].upper().endswith('.ELF') else sys.argv[2]
        base = base_of(open(elf, 'rb').read())
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
        v = BASES.get(hashlib.md5(base).hexdigest()) if base else None
        if not v:
            sys.exit(f'{sys.argv[2]}: not a version of MGO1 this build knows (a patched disc?)')
        print(v)
        return 0
    if '--any' in sys.argv: sys.argv.remove('--any'); sys.argv.append('--any')
    args = [a for a in sys.argv[1:] if a != '--any']
    if len(args) < 2:
        print(__doc__)
        return 2
    import tempfile
    tmp = tempfile.mkdtemp(prefix='mgo1_')
    if len(args) >= 3:
        elf, dat, game = args[0:3]
    else:
        (elf, dat), game = from_disc(args[0], tmp), args[1]
    # standalone install (mgo1\\MGO1Loader.dll) or under an ASI loader (plugins\\MGO1Loader.asi)
    legacy = os.path.exists(os.path.join(game, 'plugins', 'MGO1Loader.asi')) and \
        not os.path.exists(os.path.join(game, 'mgo1', 'MGO1Loader.dll'))
    out = os.path.join(game, 'plugins', 'mgo1', 'ps2') if legacy else os.path.join(game, 'mgo1', 'ps2')
    os.makedirs(out, exist_ok=True)
    base = base_of(open(elf, 'rb').read())
    if base is None:
        sys.exit('no compressed base image found in ' + elf)
    region = BASES.get(hashlib.md5(base).hexdigest())
    if not region and '--any' not in sys.argv:
        sys.exit(f'{elf}: an MGS3_N.ELF of another version (a patched disc?): the disc as it came is needed')
    print(f'the {region or "unknown"} program')
    open(os.path.join(out, 'mgs3_n_base.bin'), 'wb').write(base)
    print(f'mgs3_n_base.bin: {len(base)} bytes, md5 {hashlib.md5(base).hexdigest()}')
    with stage_dat.StageArchive(dat) as archive:
        if not any(s.name == 'ntmenu2' for s in archive.stages):
            sys.exit('no ntmenu2 page in ' + dat)
        for st in archive.stages:
            bins = st.files({1})                # a page's code (type 1)
            if len(bins) != 1:
                continue
            ov = bins[0].data
            name = f'{st.name}_{bins[0].name_hash:06x}.bin'
            open(os.path.join(out, name), 'wb').write(ov)
            print(f'{name}: {len(ov)} bytes, md5 {hashlib.md5(ov).hexdigest()}')
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print(f'done: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
