#!/usr/bin/env python3
"""Extract the IOP modules of an MGO1 module package (MODULE.IPK / MODNCD.IPK).

usage: ipk_extract.py <package.ipk> <out dir>

Entries: {u32 size, u32 name_offset, u32 args_length, u32 0} + body; every word
is XORed with a running key that restarts at 0x7E8A6B4C for each entry header
and grows by 0xA84E6B2E per word into the body (PLD.IRX, the IOP loader).
"""
import os
import struct
import sys


def main():
    data = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pos = 0
    while pos + 16 <= len(data):
        key = [0x7E8A6B4C]

        def dec(buf):
            n = len(buf) // 4
            w = list(struct.unpack_from('<%dI' % n, buf))
            for i in range(n):
                w[i] ^= key[0]
                key[0] = (key[0] + 0xA84E6B2E) & 0xFFFFFFFF
            return struct.pack('<%dI' % n, *w)

        size, name_off, args_len, _ = struct.unpack('<4I', dec(data[pos:pos + 16]))
        pos += 16
        if size == 0:
            break
        body = dec(data[pos:pos + size])
        pos += size
        name = body[name_off:name_off + 64].split(b'\0')[0].decode('latin1')
        args = body[name_off + len(name) + 1:name_off + args_len]
        open(os.path.join(out, os.path.basename(name.replace('\\', '/'))), 'wb').write(body[:name_off])
        print('%-14s %7d bytes  args %s' % (name, name_off, args.replace(b'\0', b' ').decode('latin1').strip()))


if __name__ == '__main__':
    main()
