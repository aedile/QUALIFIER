#!/usr/bin/env python3
"""Convert the MAME 'polepos' ROM set into a C header for QUALIFIER.

usage: convert_roms.py <romdir> [outdir]   (default outdir: ../main/roms)

Graphics are decoded with MAME's gfx_layout definitions (one byte per pixel):
  pp_chars[128*64], pp_tiles[128*64] (2bpp 8x8), pp_sprites[128*256] (4bpp 16x16),
  pp_bigsprites[128*1024] (4bpp 32x32).
CPU ROMs: pp_rom_z80[0x3000], pp_rom_sub1[0x4000], pp_rom_sub2[0x4000] (big-endian
word order as the Z8002 sees them: byte 0 = high byte of word 0).
"""
import sys, zlib
from pathlib import Path

EXPECTED = {
    'pp3_9.6h': (0x2000, 0xc0511173), 'pp1_10b.5h': (0x1000, 0x7174bcb7),
    'pp3_1.8m': (0x2000, 0x65c1c2c2), 'pp3_2.8l': (0x2000, 0xfafb9049),
    'pp3_5.4m': (0x2000, 0x46e5c99a), 'pp3_6.4l': (0x2000, 0xacc1ebc3),
    'pp3_28.1f': (0x1000, 0x2e77187e), 'pp1_29.1e': (0x1000, 0x706e888a),
    'pp3_25.1n': (0x2000, 0xb52c086b), 'pp3_26.1m': (0x2000, 0xd24a5707),
    'pp1_17.5n': (0x2000, 0x2e134b46), 'pp1_19.4n': (0x2000, 0x43ff83e1), 'pp1_21.3n': (0x2000, 0x5f958eb4),
    'pp1_18.5m': (0x2000, 0x6f9997d2), 'pp1_20.4m': (0x2000, 0xec18075b), 'pp1_22.3m': (0x2000, 0x1d2f30b1),
    'pp1_30.3a': (0x2000, 0xee6b3315), 'pp1_31.2a': (0x2000, 0x6d1e7042), 'pp1_32.1a': (0x1000, 0x4e97f101),
    'pp1_27.1l': (0x1000, 0xa61bff15),
    'pp1-7.8l': (0x100, 0xf07ff2ad), 'pp1-8.9l': (0x100, 0xadbde7d7), 'pp1-9.10l': (0x100, 0xddac786a),
    'pp2-10.2h': (0x100, 0x1e8d0491), 'pp1-11.4d': (0x100, 0x0e4fe8a0),
    'pp1-15.9a': (0x100, 0x2d502464), 'pp1-16.10a': (0x100, 0x027aa62c), 'pp1-17.11a': (0x100, 0x1f8d0df3),
    'pp1-12.3c': (0x400, 0x7afc7cfc), 'pp3-6.6m': (0x400, 0x63fb6057),
    'pp1-5.3b': (0x100, 0x8568decc),
    'pp1_15.6a': (0x2000, 0xb5ad4d5f), 'pp1_16.5a': (0x2000, 0x8fdd2f6f),
    'pp2_11.2e': (0x2000, 0x5b4cf05e), 'pp2_12.2f': (0x2000, 0x32b694c2), 'pp2_13.1e': (0x2000, 0x8842138a),
}

def load(romdir, name):
    data = (romdir / name).read_bytes()
    size, crc = EXPECTED[name]
    if len(data) != size:
        sys.exit(f'{name}: {len(data)} bytes, expected {size}')
    if zlib.crc32(data) & 0xffffffff != crc:
        print(f'  warning: {name} crc32 {zlib.crc32(data):08x} != expected {crc:08x}')
    return data

def decode(data, width, height, planes, xoff, yoff, inc_bits, count):
    out = bytearray()
    for c in range(count):
        base = c * inc_bits
        for y in range(height):
            for x in range(width):
                v = 0
                for p, poff in enumerate(planes):
                    off = base + yoff[y] + xoff[x] + poff
                    v |= ((data[off >> 3] >> (7 - (off & 7))) & 1) << p
                out.append(v)
    return bytes(out)

def interleave16(lo_file, hi_file):
    """ROM_LOAD16_BYTE: file A at odd bytes, file B at even bytes -> big-endian words"""
    out = bytearray(len(lo_file) * 2)
    out[0::2] = hi_file
    out[1::2] = lo_file
    return bytes(out)

def c_array(name, data, dims):
    lines = [f'static const uint8_t {name}{dims} = {{']
    for i in range(0, len(data), 24):
        lines.append('    ' + ','.join(f'0x{b:02X}' for b in data[i:i+24]) + ',')
    lines.append('};')
    return '\n'.join(lines)

def main():
    romdir = Path(sys.argv[1] if len(sys.argv) > 1 else 'polepos')
    outdir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / 'main' / 'roms'
    outdir.mkdir(parents=True, exist_ok=True)
    print(f'ROMs: {romdir}')
    z80 = load(romdir, 'pp3_9.6h') + load(romdir, 'pp1_10b.5h')
    sub1 = interleave16(load(romdir, 'pp3_1.8m'), load(romdir, 'pp3_2.8l'))   # .8m at 0x0001, .8l at 0x0000
    sub2 = interleave16(load(romdir, 'pp3_5.4m'), load(romdir, 'pp3_6.4l'))
    xo2 = [0, 1, 2, 3, 64, 65, 66, 67]; yo2 = [y * 8 for y in range(8)]
    chars = decode(load(romdir, 'pp3_28.1f'), 8, 8, [0, 4], xo2, yo2, 128, 256)
    tiles = decode(load(romdir, 'pp1_29.1e'), 8, 8, [0, 4], xo2, yo2, 128, 256)
    spr = load(romdir, 'pp3_25.1n') + load(romdir, 'pp3_26.1m')
    half = len(spr) * 8 // 2
    xo4s = [0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27]
    sprites = decode(spr, 16, 16, [0, 4, half, half + 4], xo4s, [y * 32 for y in range(16)], 512, 128)
    big = (load(romdir, 'pp1_17.5n') + load(romdir, 'pp1_19.4n') + load(romdir, 'pp1_21.3n') + bytes(0x2000) +
           load(romdir, 'pp1_18.5m') + load(romdir, 'pp1_20.4m') + load(romdir, 'pp1_22.3m') + bytes(0x2000))
    halfb = len(big) * 8 // 2
    xo4b = [b for g in range(8) for b in (g * 8, g * 8 + 1, g * 8 + 2, g * 8 + 3)]
    bigsprites = decode(big, 32, 32, [0, 4, halfb, halfb + 4], xo4b, [y * 64 for y in range(32)], 2048, 128)
    road = load(romdir, 'pp1_30.3a') + load(romdir, 'pp1_31.2a') + load(romdir, 'pp1_32.1a')
    proms = (load(romdir, 'pp1-7.8l') + load(romdir, 'pp1-8.9l') + load(romdir, 'pp1-9.10l') + load(romdir, 'pp2-10.2h') +
             load(romdir, 'pp1-11.4d') + load(romdir, 'pp1-15.9a') + load(romdir, 'pp1-16.10a') + load(romdir, 'pp1-17.11a') +
             load(romdir, 'pp1-12.3c') + load(romdir, 'pp3-6.6m'))
    out = ['/* polepos_roms.h - AUTO-GENERATED by tools/convert_roms.py - DO NOT COMMIT */',
           '#ifndef POLEPOS_ROMS_H', '#define POLEPOS_ROMS_H', '#include <stdint.h>', '',
           c_array('pp_rom_z80', z80, '[0x3000]'), c_array('pp_rom_sub1', sub1, '[0x8000]'), c_array('pp_rom_sub2', sub2, '[0x8000]'),
           c_array('pp_chars', chars, '[256 * 64]'), c_array('pp_tiles', tiles, '[256 * 64]'),
           c_array('pp_sprites', sprites, '[128 * 256]'), c_array('pp_bigsprites', bigsprites, '[128 * 1024]'),
           c_array('pp_road', road, '[0x5000]'), c_array('pp_scalelut', load(romdir, 'pp1_27.1l'), '[0x1000]'),
           c_array('pp_proms', proms, '[0x1000]'), c_array('pp_wave', load(romdir, 'pp1-5.3b'), '[0x100]'),
           c_array('pp_engine', load(romdir, 'pp1_15.6a') + load(romdir, 'pp1_16.5a'), '[0x4000]'),
           c_array('pp_voice', load(romdir, 'pp2_11.2e') + load(romdir, 'pp2_12.2f') + load(romdir, 'pp2_13.1e'), '[0x6000]'),
           '#endif']
    (outdir / 'polepos_roms.h').write_text('\n'.join(out))
    print(f'wrote {outdir / "polepos_roms.h"} ({(outdir / "polepos_roms.h").stat().st_size // 1024} KB)')

if __name__ == '__main__':
    main()
