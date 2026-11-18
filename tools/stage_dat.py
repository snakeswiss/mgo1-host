#!/usr/bin/env python3
"""STAGE_N.DAT, MGO1's stage archive, read as docs/STAGE_DAT.md describes it (the same format as
MGO1Loader/stagedat.cpp and extramaps.cpp read and write).

    python3 tools/stage_dat.py list <STAGE_N.DAT> [stage]          the stages, or one stage's sections and files
    python3 tools/stage_dat.py extract <STAGE_N.DAT> <stage> <dir>  a stage's files into dir (a7b7fb.tri ...)
    python3 tools/stage_dat.py check <STAGE_N.DAT> ...              every stage read: its blocks inflated to their
                                                                    sections' sizes and crypted back to the same
                                                                    bytes, its end at the next stage's start

As a module:

    with StageArchive(path) as dat:
        for st in dat.stages: ...                 # name, sector, offset
        for f in dat.stage('n003a').files(): ...  # tag, type, name_hash, ext, filename, data

The archive may be a span of a larger file (a disc image): StageArchive(path, base, size).
"""
import os, struct, sys, zlib

SECTOR = 0x800
LCG_MUL = 0x02E90EDD          # the key stream: key' = key * LCG_MUL + step (32 bits)
TOC_ADD = 0xA78925D9          # a stage's table of contents: first key, with its name's code and the archive's key
TOC_STEP = 0x7A88FB59         # ... and step
BLOCK_KEY = 0x9385            # a block's key: its first two bytes; v = key ^ BLOCK_KEY
BLOCK_HIGH = 0x6576           # the stream's first key: v | (v ^ BLOCK_HIGH) << 16
BLOCK_STEP = 0x0116           # its step: v * BLOCK_STEP
ZLIB_HEAD = b'\x78\x9c'       # the zlib header a block's first two bytes stand for

SECTION = 0x7F                # entry tag's top byte: a section (its value: its size), 0x7F000000 its end
BLOCK = 0x7E                  # a compressed block (low 24 bits: its size; value: its place in the data area)
PLAIN = {0x7F000004, 0x7F000005, 0x7F000010}   # sections stored as they are (no block)

# a file's type (its tag's top byte) and the extension it is named with (as extramaps.cpp's TypeOf)
TYPES = {0x00: 'sar', 0x01: 'bin', 0x02: 'gcx', 0x03: 'tri', 0x04: 'mdh', 0x05: 'mds', 0x06: 'lt2', 0x07: 'cv2',
         0x08: 'mtar', 0x09: 'mtsq', 0x0a: 'mtfa', 0x0b: 'mtcm', 0x0c: 'geom', 0x0d: 'mdl', 0x0f: 'nav', 0x10: 'cvd',
         0x11: 'eft', 0x12: 'zon', 0x13: 'rpd', 0x5e: 'ico', 0x5f: 'la2', 0x68: 'var', 0x69: 'img', 0x6a: 'vib',
         0x6b: 'rat', 0x6c: 'rcm', 0x6d: 'ola', 0x6e: 'raw', 0x6f: 'mtra'}
EXTS = {e: t for t, e in TYPES.items()}


def ext_of(type_):
    return TYPES.get(type_, f'{type_:02x}')


def type_of(ext):
    """a file name's extension -> its type (two hex digits for a type without a name), None when none"""
    ext = ext.lower()
    if ext in EXTS:
        return EXTS[ext]
    if len(ext) == 2 and all(c in '0123456789abcdef' for c in ext):
        return int(ext, 16)
    return None


def strcode24(name):
    """a name's 24-bit code: rotate left 5 within 24 bits, add the byte; 0 counts as 1"""
    h = 0
    for c in name.encode('latin-1'):
        h = ((h << 5) | (h >> 19)) & 0xFFFFFF
        h = (h + c) & 0xFFFFFF
    return h or 1


def _stream(key, step, n):
    """n words of the key stream from key"""
    try:
        import numpy as np
    except ImportError:
        out = []
        for _ in range(n):
            out.append(key)
            key = (key * LCG_MUL + step) & 0xFFFFFFFF
        return out
    # key_i = A^i key + step (A^0 + .. + A^(i-1)): both terms built by doubling
    m32 = np.uint64(0xFFFFFFFF)
    powers = np.ones(1, np.uint64)
    sums = np.zeros(1, np.uint64)
    a = np.uint64(LCG_MUL)                     # A^len(powers)
    while len(powers) < n:
        s = (sums[-1] * np.uint64(LCG_MUL) + np.uint64(1)) & m32     # A^0 + .. + A^(len-1)
        powers = np.concatenate([powers, (powers * a) & m32])
        sums = np.concatenate([sums, (s + ((sums * a) & m32)) & m32])
        a = (a * a) & m32
    powers, sums = powers[:n], sums[:n]
    return (((powers * np.uint64(key)) & m32) + ((sums * np.uint64(step)) & m32)) & m32


def crypt(data, key, step):
    """the archive's crypt (the same both ways): each 32-bit word XORed with the key stream from key; a
    tail under 4 bytes as it is"""
    n = len(data) // 4
    if not n:
        return bytes(data)
    ks = _stream(key & 0xFFFFFFFF, step & 0xFFFFFFFF, n)
    try:
        import numpy as np
        w = np.frombuffer(bytes(data[:4 * n]), '<u4') ^ np.asarray(ks, np.uint64).astype('<u4')
        return w.tobytes() + bytes(data[4 * n:])
    except ImportError:
        w = struct.unpack(f'<{n}I', bytes(data[:4 * n]))
        return struct.pack(f'<{n}I', *(x ^ k for x, k in zip(w, ks))) + bytes(data[4 * n:])


def header_crypt(data, key):
    """the header and stage table (from the file's byte 4): the archive's key, step key ^ 0xF0F0"""
    return crypt(data, key, key ^ 0xF0F0)


def toc_crypt(data, name, key):
    """a stage's table of contents: keyed by its name's code and the archive's key"""
    h = strcode24(name)
    return crypt(data, ((h << 7) + key + h + TOC_ADD) & 0xFFFFFFFF, ((h << 7) + h + TOC_STEP) & 0xFFFFFFFF)


def _block_stream(key16):
    v = (key16 ^ BLOCK_KEY) & 0xFFFF
    return v | ((v ^ BLOCK_HIGH) << 16), v * BLOCK_STEP


def block_decrypt(z):
    """a compressed block as stored (its bytes to a multiple of 4) -> its zlib stream: the first two bytes
    hold the block's key and stand for the zlib header"""
    if len(z) < 4:
        raise ValueError('a block under 4 bytes')
    key16 = z[0] | z[1] << 8
    p = crypt(z, *_block_stream(key16))
    if p[:2] != BLOCK_KEY.to_bytes(2, 'little'):
        raise ValueError('not a block')
    return ZLIB_HEAD + p[2:]


def block_encrypt(stream, key16):
    """a zlib stream (78 9C, padded to a multiple of 4) -> a block as stored with that key"""
    if stream[:2] != ZLIB_HEAD:
        raise ValueError('a zlib stream starts 78 9C')
    return crypt(BLOCK_KEY.to_bytes(2, 'little') + bytes(stream[2:]), *_block_stream(key16))


def up(v, a):
    return (v + a - 1) & ~(a - 1)


class StageFile:
    """one file of a stage: its tag (type << 24 | the code of its name), its bytes (to the next file's start)"""
    def __init__(self, tag, data, section):
        self.tag, self.data, self.section = tag, data, section

    type = property(lambda self: self.tag >> 24)
    name_hash = property(lambda self: self.tag & 0xFFFFFF)
    ext = property(lambda self: ext_of(self.tag >> 24))
    filename = property(lambda self: f'{self.tag & 0xFFFFFF:06x}.{ext_of(self.tag >> 24)}')

    def __repr__(self):
        return f'<{self.filename} {len(self.data)} bytes>'


class Section:
    """a section of a stage: its tag, its size (inflated), its block (place in the data area and size, or
    for a plain one its place and size as stored), its files' entries {tag, place in the section}"""
    def __init__(self, tag, size):
        self.tag, self.size = tag, size
        self.plain = tag in PLAIN
        self.at = self.stored = None
        self.entries = []


class Stage:
    def __init__(self, archive, name, sector):
        self.archive, self.name, self.sector = archive, name, sector
        self.offset = sector * SECTOR
        self._toc = None

    def __repr__(self):
        return f'<stage {self.name} at sector {self.sector:#x}>'

    @property
    def toc(self):
        """the table of contents: {tag, value} entries (its count word left out, the closing {0, 0} kept)"""
        if self._toc is None:
            a = self.archive
            count = struct.unpack('<I', toc_crypt(a.read(self.offset, 4), self.name, a.key))[0]
            if not 0 < count < 100000:
                raise ValueError(f'{self.name}: its table of contents does not read ({count} entries)')
            raw = toc_crypt(a.read(self.offset, 4 + 8 * count), self.name, a.key)
            self._toc = [struct.unpack_from('<II', raw, 4 + 8 * i) for i in range(count)]
        return self._toc

    @property
    def data_at(self):
        """the data area's place in the archive: after the table of contents, at a sector"""
        return self.offset + up(4 + 8 * len(self.toc), SECTOR)

    @property
    def sections(self):
        out, cur, high = [], None, 0
        for tag, val in self.toc:
            if tag == 0 and val == 0:
                break
            top = tag >> 24
            if tag == 0x7F000000:
                cur = None
            elif top == SECTION:
                cur = Section(tag, val)
                out.append(cur)
                if cur.plain:                             # stored after all before it, at a sector
                    cur.at, cur.stored = up(high, SECTOR), val
                    high = cur.at + val
            elif top == BLOCK:
                if cur is None or cur.plain or cur.at is not None:
                    raise ValueError(f'{self.name}: a block outside a compressed section')
                cur.at, cur.stored = val, tag & 0xFFFFFF
                high = max(high, val + up(cur.stored, 4))
            else:
                if cur is None or cur.at is None:
                    raise ValueError(f'{self.name}: a file outside a section')
                cur.entries.append((tag, val))
        self._high = high
        return out

    @property
    def extent(self):
        """the bytes the stage spans in the archive: its table of contents, its data to a sector"""
        self.sections
        return up(4 + 8 * len(self.toc), SECTOR) + up(self._high, SECTOR)

    def section_bytes(self, sec):
        """a section's bytes as the game has them in memory (a compressed one inflated)"""
        a = self.archive
        if sec.plain:
            return a.read(self.data_at + sec.at, sec.stored)
        z = block_decrypt(a.read(self.data_at + sec.at, up(sec.stored, 4)))
        return zlib.decompressobj().decompress(z[:sec.stored])

    def files(self, types=None):
        """the stage's files, in its order; types: only the sections holding a file of those types are read"""
        out = []
        for sec in self.sections:
            if not sec.entries or (types is not None and not any(t >> 24 in types for t, _ in sec.entries)):
                continue
            data = self.section_bytes(sec)
            for k, (tag, at) in enumerate(sec.entries):
                end = sec.entries[k + 1][1] if k + 1 < len(sec.entries) else len(data)
                if not at <= end <= len(data):
                    raise ValueError(f'{self.name}: a file outside its section')
                if types is None or tag >> 24 in types:
                    out.append(StageFile(tag, data[at:end], sec.tag))
        return out

    def file(self, filename):
        t = type_of(filename.rsplit('.', 1)[-1])
        for f in self.files({t}):
            if f.filename == filename.lower():
                return f
        return None


class StageArchive:
    """STAGE_N.DAT: its key (the first word, in clear), three header words, the stage table"""
    def __init__(self, path, base=0, size=None):
        self.path, self.base = path, base
        self.f = open(path, 'rb')
        self.size = size if size is not None else os.fstat(self.f.fileno()).st_size - base
        self.key = struct.unpack('<I', self.read(0, 4))[0]
        head = header_crypt(self.read(4, 12), self.key)
        self.header = struct.unpack('<3I', head)
        count = self.header[1] & 0xFFFF
        if not 0 < count <= 512 or self.header[0] != 0x00010001:
            raise ValueError(f'{path}: not a stage archive')
        table = header_crypt(self.read(4, 12 + 20 * count), self.key)
        self.stages = []
        for i in range(count):
            e = table[12 + 20 * i:32 + 20 * i]
            self.stages.append(Stage(self, e[:16].split(b'\0', 1)[0].decode('latin-1'), struct.unpack_from('<I', e, 16)[0]))

    def read(self, at, n):
        if at < 0 or at + n > self.size:
            raise ValueError(f'{self.path}: a read past its end ({at:#x} + {n:#x})')
        self.f.seek(self.base + at)
        b = self.f.read(n)
        if len(b) != n:
            raise ValueError(f'{self.path}: cut')
        return b

    def stage(self, name):
        for st in self.stages:
            if st.name == name:
                return st
        raise KeyError(f'{self.path}: no stage {name}')

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def check(path):
    """every stage of an archive read (sizes, crypt round trip, extents): the problems found, and notes (a
    stage that ends before the next one: a rebuilt archive's room)"""
    bad, notes = [], []
    with StageArchive(path) as dat:
        order = sorted(dat.stages, key=lambda s: s.sector)
        for i, st in enumerate(order):
            try:
                for sec in st.sections:
                    stored = dat.read(st.data_at + sec.at, sec.stored if sec.plain else up(sec.stored, 4))
                    if sec.plain:
                        continue
                    z = block_decrypt(stored)
                    if block_encrypt(z, stored[0] | stored[1] << 8) != stored:
                        bad.append(f'{st.name}: section {sec.tag:08x}: the crypt does not come back')
                    d = zlib.decompressobj()
                    n = len(d.decompress(z[:sec.stored]))
                    if not d.eof or n != sec.size:
                        bad.append(f'{st.name}: section {sec.tag:08x}: {n} bytes inflated for {sec.size}')
                st.files()
                end = st.offset + st.extent
                nxt = order[i + 1].offset if i + 1 < len(order) else dat.size
                if end > nxt:
                    bad.append(f'{st.name}: ends at {end:#x}, past the next stage ({nxt:#x})')
                elif end < nxt and i + 1 < len(order):
                    notes.append(f'{st.name}: ends at {end:#x}, {nxt - end:#x} bytes before the next stage')
            except (ValueError, zlib.error) as e:
                bad.append(f'{st.name}: {e}')
    return bad, notes


def main(argv):
    if len(argv) >= 2 and argv[0] == 'list':
        with StageArchive(argv[1]) as dat:
            if len(argv) == 2:
                print(f'key {dat.key:08x}, header {" ".join(f"{w:08x}" for w in dat.header)}, {len(dat.stages)} stages')
                for st in dat.stages:
                    print(f'  {st.name:16} sector {st.sector:#07x}  {st.extent:#9x} bytes  {len(st.toc) - 1} entries')
                return 0
            st = dat.stage(argv[2])
            for sec in st.sections:
                kind = 'plain' if sec.plain else f'block of {sec.stored:#x} bytes'
                print(f'section {sec.tag:08x}: {sec.size:#x} bytes, {kind} at {sec.at:#x}, {len(sec.entries)} files')
            for f in st.files():
                print(f'  {f.filename:14} {len(f.data):9} bytes  (section {f.section:08x})')
            return 0
    if len(argv) == 4 and argv[0] == 'extract':
        with StageArchive(argv[1]) as dat:
            os.makedirs(argv[3], exist_ok=True)
            files = dat.stage(argv[2]).files()
            for f in files:
                with open(os.path.join(argv[3], f.filename), 'wb') as o:
                    o.write(f.data)
            print(f'{argv[3]}: {len(files)} files')
            return 0
    if len(argv) >= 2 and argv[0] == 'check':
        worst = 0
        for p in argv[1:]:
            bad, notes = check(p)
            print(f'{p}: ' + ('every stage reads' if not bad else f'{len(bad)} problems'))
            for b in bad:
                print('  ' + b)
            for b in notes:
                print('  note: ' + b)
            worst |= bool(bad)
        return worst
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
