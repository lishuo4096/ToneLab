"""Verify all four PE files embed every exact PNG frame from the committed ICO."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
k = c.WinDLL('kernel32', use_last_error=True)
k.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
k.LoadLibraryExW.restype = w.HMODULE
k.FindResourceW.argtypes = [w.HMODULE, c.c_void_p, c.c_void_p]
k.FindResourceW.restype = w.HRSRC
k.LoadResource.argtypes = [w.HMODULE, w.HRSRC]
k.LoadResource.restype = w.HGLOBAL
k.LockResource.argtypes = [w.HGLOBAL]
k.LockResource.restype = c.c_void_p
k.SizeofResource.argtypes = [w.HMODULE, w.HRSRC]
k.SizeofResource.restype = w.DWORD
k.FreeLibrary.argtypes = [w.HMODULE]


def resource(module, name, kind):
    found = k.FindResourceW(module, name, kind)
    assert found, f'Missing resource {name} / type {kind}'
    size = k.SizeofResource(module, found)
    return c.string_at(k.LockResource(k.LoadResource(module, found)), size)


ico = (ROOT / 'assets' / 'tonelab.ico').read_bytes()
count = struct.unpack_from('<H', ico, 4)[0]
assert count == 9
expected = []
for i in range(count):
    width, height, _, _, planes, depth, size, offset = struct.unpack_from('<BBBBHHII', ico, 6+i*16)
    expected.append((width, height, size, ico[offset:offset+size]))

for name in ('ToneLab.exe', 'tonelab-cli.exe', 'dsp-tests.exe', 'loopback-test.exe'):
    module = k.LoadLibraryExW(str(ROOT / 'dist' / name), None, 2)
    assert module, f'Cannot load PE resource data: {name}'
    try:
        group = resource(module, 101, 14)
        assert struct.unpack_from('<HHH', group) == (0, 1, count)
        for i, (width, height, size, png) in enumerate(expected):
            entry = struct.unpack_from('<BBBBHHIH', group, 6+i*14)
            assert entry[0] == width and entry[1] == height and entry[6] == size
            assert resource(module, entry[7], 3) == png
        assert resource(module, 1, 16), 'Missing version information'
        print(f'PASS: {name}: 9 exact icon frames + version resource')
    finally:
        k.FreeLibrary(module)
