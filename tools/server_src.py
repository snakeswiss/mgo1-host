#!/usr/bin/env python3
"""(build 1.00, srvbuild) The sources a server builds mgo1host.exe from (host/build_server.sh, docs/DEDICATED_HOST.md
section 27): the smallest set that builds, nothing of the game.

    python3 tools/server_src.py --out <mgo1host-src.tar.gz> [--version "build 1.00"] [--official-exe <mgo1host.exe>]
    python3 tools/server_src.py --list
    python3 tools/server_src.py --check <archive or folder> ...

The set: the build's scripts (host/build_server.sh, host/build_host.sh, MGO1Loader/build_gen.sh, tools/build_opus.sh), the
generators the North American disc's code is made with and the lists they follow (addresses only: recomp/closure_*.txt,
alias_*.txt, roots.txt, variants.txt, vu_programs.txt), the files host/build_host.sh compiles and every header they include
(found by the compiler itself, x86_64-w64-mingw32-g++ -MM), Recast & Detour, Opus (third_party/opus) with their licenses.
SOURCE_INFO says the version, the commit, the time stamp the build writes (SOURCE_DATE_EPOCH: the commit's time) and, with
--official-exe, the official build's SHA-256 (the server's build says whether it made the same file).
The archive is the same bytes for the same sources (names sorted, dates the commit's, owner root, gzip without a date).
--check refuses an archive (zip, tar.gz, and the mgo1host-src.tar.gz inside one) or a folder holding a file generated from
the game (tools/source_zip.py's rule), a host executable, a disc's file or generated code.
"""
import glob, gzip, hashlib, io, os, re, struct, subprocess, sys, tarfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import source_zip                                    # noqa: E402  (its rules: GENERATED, LEAKED)

TOP = 'mgo1host-src'
SCRIPTS = ['host/build_server.sh', 'host/build_host.sh', 'MGO1Loader/build_gen.sh', 'tools/build_opus.sh']
# the generators (build_all.sh's steps for the North American disc alone) and what they read, measured (strace of a
# generation: tools/ps2_images.py, tools/gen_versions.sh, tools/page_map.py, recomp/ipk_extract.py, recomp/iop2cpp.py,
# recomp/vu2cpp.py --vu0-static)
GENERATORS = ['tools/ps2_images.py', 'tools/stage_dat.py', 'tools/gen_versions.sh', 'tools/page_heads.py', 'tools/page_map.py',
              'tools/irx.py', 'recomp/closure.py', 'recomp/mips2shape.py', 'recomp/mips2cpp.py', 'recomp/vu2cpp.py',
              'recomp/ipk_extract.py', 'recomp/iop2cpp.py', 'recomp/roots.txt', 'recomp/variants.txt', 'recomp/vu_programs.txt',
              'recomp/closure_base.txt', 'recomp/closure_functions.txt']
PAGES = 'init_n r_menu_n r_sna01_n ntmenu n001a n002a n003a n011a n012a n013a n021a n022a n024a n031a n032a n033a'.split()
LICENSES = ['MGO1Loader/recast/License.txt', 'third_party/opus/COPYING']
CXX = 'x86_64-w64-mingw32-g++'
README = """These are the sources MGO1 Host's server build compiles mgo1host.exe from, on your server, with your own MGO1 disc
(host/build_server.sh; install.sh runs it when the package holds no mgo1host.exe). They hold no code and no data of the
game: the game's code is generated on your server from the disc's files, and stays there.
The project's whole sources and their documents: the source package of the release (docs/DEDICATED_HOST.md, section 27).
Licenses: Recast & Detour (MGO1Loader/recast/License.txt, zlib), Opus (third_party/opus/COPYING, BSD).
"""


def git(*args):
    return subprocess.run(['git', '-C', ROOT] + list(args), capture_output=True, text=True).stdout.strip()


def host_units():
    """the files host/build_host.sh compiles (read from it, so that a file added there is in the package)"""
    text = open(os.path.join(ROOT, 'host', 'build_host.sh')).read()
    units = ['MGO1Loader/' + f for f in re.findall(r'-c (\w+\.cpp)', text)]
    m = re.search(r'^for f in ([\w ]+); do \$CXX -O2 -c \$f\.cpp', text, re.M)
    if not m:
        sys.exit('server_src.py: the platform files\' loop not found in host/build_host.sh')
    units += ['MGO1Loader/%s.cpp' % f for f in m.group(1).split()]
    if 'recast/*.cpp' in text:
        units += sorted(os.path.relpath(p, ROOT) for p in glob.glob(os.path.join(ROOT, 'MGO1Loader', 'recast', '*.cpp')))
    m = re.search(r'-c \.\./(tests/\w+\.cpp)', text)
    if not m:
        sys.exit('server_src.py: the harness\'s main not found in host/build_host.sh')
    units.append(m.group(1))
    return units


def header_deps(units):
    """every file the compiler reads for these units and for the generated code (ps2rt.h, vu_micro.h, iop_rt.h)"""
    L = os.path.join(ROOT, 'MGO1Loader')
    base = [CXX, '-std=c++17', '-DMINGW_HAS_SECURE_API=1', '-I' + L, '-I' + os.path.join(L, 'gl'), '-w', '-MM']
    import tempfile
    tmp = tempfile.mkdtemp(prefix='server_src_')
    stub = os.path.join(tmp, 'stub.cpp')                 # (the generated code's headers, as a generated file includes them)
    open(stub, 'w').write('#include "ps2rt.h"\n#include "vu_micro.h"\n#include "iop_rt.h"\n')
    found = set()
    try:
        for u in units + [stub]:
            extra = ['-I' + os.path.join(ROOT, 'third_party', 'opus', 'include')] if u.startswith('tests/') else []
            p = subprocess.run(base + extra + [u], cwd=ROOT, capture_output=True, text=True)
            if p.returncode != 0:
                sys.exit('server_src.py: %s -MM %s failed: %s' % (CXX, u, p.stderr[-400:]))
            for tok in p.stdout.replace('\\\n', ' ').split():
                if tok.endswith(':'):
                    continue
                rel = os.path.relpath(os.path.realpath(os.path.join(ROOT, tok)), ROOT)
                if not rel.startswith('..'):
                    found.add(rel)
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    return found


def file_list():
    try:
        subprocess.run([CXX, '--version'], capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        sys.exit('server_src.py: %s is needed (it finds the headers the host includes)' % CXX)
    files = set(SCRIPTS + GENERATORS + LICENSES)
    for p in PAGES:
        files.add('recomp/closure_%s.txt' % p)
        files.add('recomp/alias_%s.txt' % p)
    units = host_units()
    files.update(units)
    files.update(header_deps(units))
    files.update(os.path.relpath(p, ROOT) for p in glob.glob(os.path.join(ROOT, 'MGO1Loader', 'recast', '*.h')))
    for d in ('celt', 'silk', 'silk/float', 'src', 'include'):              # tools/build_opus.sh's sources and headers
        for p in glob.glob(os.path.join(ROOT, 'third_party', 'opus', d, '*.[ch]')):
            files.add(os.path.relpath(p, ROOT))
    missing = sorted(f for f in files if not os.path.isfile(os.path.join(ROOT, f)))
    if missing:
        sys.exit('server_src.py: not in the tree: ' + ', '.join(missing))
    return sorted(files)


BAD_NAMES = re.compile(r'(^|/)(mgo1host\.exe(\.gz)?|MGO1_core\.dll|MGS3_N\.ELF|STAGE_N\.DAT|SLOT_N\.DAT|[^/]*\.(ipk|irx|elf|iso|img))$', re.I)
BAD_DIRS = re.compile(r'(^|/)(gen|obj|objO2|images|ipk)/')


def check_blob(name, data, bad):
    if BAD_NAMES.search(name):
        bad.append(name + ': a host executable or a disc\'s file')
    elif BAD_DIRS.search(name) and not name.startswith('MGO1 Host/licenses/'):
        bad.append(name + ': generated code or the disc\'s images')
    elif source_zip.GENERATED.search(data[:4096]):
        bad.append(name + ': generated from the game')
    elif source_zip.LEAKED.search(data):
        bad.append(name + ': says it was made from a leak')
    if name.endswith('mgo1host-src.tar.gz'):
        check_tar(io.BytesIO(data), name + '!', bad)


def check_tar(f, prefix, bad):
    with tarfile.open(fileobj=f, mode='r:*') as t:
        for m in t.getmembers():
            if m.isfile():
                check_blob(prefix + m.name, t.extractfile(m).read(), bad)


def check(paths):
    bad, n = [], [0]
    for p in paths:
        if os.path.isdir(p):
            for base, _, names in os.walk(p):
                for nm in names:
                    full = os.path.join(base, nm)
                    check_blob(os.path.relpath(full, p).replace(os.sep, '/'), open(full, 'rb').read(), bad)
        elif p.endswith('.zip'):
            with zipfile.ZipFile(p) as z:
                for nm in z.namelist():
                    if not nm.endswith('/'):
                        check_blob(nm, z.read(nm), bad)
        else:
            with open(p, 'rb') as f:
                check_tar(f, '', bad)
        print('%s: checked' % p)
    if bad:
        print('REFUSED:\n  ' + '\n  '.join(bad[:40]))
        return 1
    print('no file of the game, no host executable, no disc file, no generated code')
    return 0


def pe_stamp(path):
    d = open(path, 'rb').read(4096)
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    return struct.unpack_from('<I', d, pe + 8)[0]


def write(out, version, official):
    files = file_list()
    commit = git('rev-parse', 'HEAD') or 'unknown'
    epoch = int(git('log', '-1', '--format=%ct') or 0)
    dirty = git('status', '--porcelain', '--untracked-files=no', '--', *files)
    info = ['# MGO1 Host\'s server build (host/build_server.sh): what these sources are',
            'version=%s' % version, 'commit=%s%s' % (commit, ' (with changes not committed)' if dirty else ''),
            'source_date_epoch=%d' % epoch]
    if official:
        info.append('official_sha256=%s' % hashlib.sha256(open(official, 'rb').read()).hexdigest())
        info.append('official_stamp=%d' % pe_stamp(official))
    extra = {'SOURCE_INFO': '\n'.join(info) + '\n', 'README.txt': README}
    bad = []
    for rel in files:
        check_blob(rel, open(os.path.join(ROOT, rel), 'rb').read(), bad)
    if bad:
        sys.exit('server_src.py: refused: ' + '; '.join(bad))
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode='w', format=tarfile.USTAR_FORMAT) as t:
        def add(rel, data, mode):
            ti = tarfile.TarInfo('%s/%s' % (TOP, rel))
            ti.size, ti.mode, ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname = len(data), mode, epoch, 0, 0, 'root', 'root'
            t.addfile(ti, io.BytesIO(data))
        for rel in sorted(list(extra) + files):
            if rel in extra:
                add(rel, extra[rel].encode(), 0o644)
            else:
                full = os.path.join(ROOT, rel)
                add(rel, open(full, 'rb').read(), 0o755 if os.access(full, os.X_OK) and rel.endswith('.sh') else 0o644)
    with open(out, 'wb') as f:
        with gzip.GzipFile(filename='', mode='wb', fileobj=f, compresslevel=9, mtime=0) as g:
            g.write(buf.getvalue())
    size = sum(os.path.getsize(os.path.join(ROOT, r)) for r in files)
    print('%s: %d files (%d bytes), %d bytes compressed, commit %s%s' % (out, len(files) + len(extra), size, os.path.getsize(out),
                                                                        commit[:12], ' DIRTY' if dirty else ''))
    return 0


def main():
    a = sys.argv[1:]
    if a[:1] == ['--list']:
        print('\n'.join(file_list()))
        return 0
    if a[:1] == ['--check'] and len(a) > 1:
        return check(a[1:])
    if '--out' in a:
        out = a[a.index('--out') + 1]
        version = a[a.index('--version') + 1] if '--version' in a else 'build 1.00'
        official = a[a.index('--official-exe') + 1] if '--official-exe' in a else ''
        return write(out, version, official)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
