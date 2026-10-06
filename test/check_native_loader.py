"""Check the shipped PE, independently of the compiler's link flags."""
import struct
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_bytes()


def u16(offset):
    return struct.unpack_from('<H', data, offset)[0]


def u32(offset):
    return struct.unpack_from('<I', data, offset)[0]


pe = u32(0x3c)
assert data[pe:pe + 4] == b'PE\0\0', 'not a PE executable'
machine = u16(pe + 4)
assert machine in (0x14c, 0x8664), 'expected Win32 or Win64'
optional = pe + 24
directory = optional + (112 if u16(optional) == 0x20b else 96)
sections = optional + u16(pe + 20)


def file_offset(rva):
    for index in range(u16(pe + 6)):
        section = sections + index * 40
        base, size, raw = u32(section + 12), u32(section + 16), u32(section + 20)
        if base <= rva < base + size:
            return raw + rva - base
    raise AssertionError(f'unmapped RVA {rva:x}')


descriptor = file_offset(u32(directory + 8))
imports = set()
while u32(descriptor + 12):
    name = file_offset(u32(descriptor + 12))
    imports.add(data[name:data.index(b'\0', name)].decode('ascii').lower())
    descriptor += 20
assert imports == {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll', 'winmm.dll'}, imports
assert u32(directory + 13 * 8) == 0, 'unexpected delay imports'
assert u32(optional + 16) != 0, 'missing entry point'
assert u16(optional + 68) == 2, 'expected Windows GUI subsystem'
# Guard against accidentally materializing the large zero-initialized buffers,
# adding SDL/static CRT, or leaving debug data in the distributed executable.
assert len(data) < 32768, f'loader size regression: {len(data)} bytes'
print(f'{"Win64" if machine == 0x8664 else "Win32"}: {len(data)} bytes; only GDI/WinMM and Windows system imports')
